#include "imu.h"
#include "adc.h" // for adcGetSampleCounter()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

// Global instance
SparkFun_LSM6DSV16X g_imu;

// Optional interrupt flags (unused for now)
static volatile bool g_imuInt1Fired = false;
static volatile bool g_imuInt2Fired = false;

static void IRAM_ATTR imuInt1ISR()
{
    g_imuInt1Fired = true;
}

static void IRAM_ATTR imuInt2ISR()
{
    g_imuInt2Fired = true;
}

// --- IMU ring buffer ---

static const size_t IMU_RING_BUFFER_SIZE = 1024;
static const size_t IMU_RING_BUFFER_MASK = IMU_RING_BUFFER_SIZE - 1;

static ImuSample imuRingBuffer[IMU_RING_BUFFER_SIZE];
static volatile uint32_t imuRingHead = 0; // next write index
static volatile uint32_t imuRingTail = 0; // next read index
static volatile uint32_t imuSampleIndexCounter = 0;
static volatile uint32_t imuOverflowCount = 0;

static TaskHandle_t imuTaskHandle = nullptr;

// Internal helper to push IMU sample into ring
static inline void imuRingPush(const ImuSample &src)
{
    uint32_t head = imuRingHead;
    uint32_t nextHead = (head + 1U) & IMU_RING_BUFFER_MASK;
    uint32_t tail = imuRingTail;

    if (nextHead == tail)
    {
        // Buffer full, drop sample
        imuOverflowCount = imuOverflowCount + 1;
        return;
    }

    imuRingBuffer[head] = src;
    imuRingHead = nextHead;
}

bool imuGetNextSample(ImuSample &sample)
{
    // Read head first, then tail (acquire semantics)
    // This ensures we see the most recent head value
    uint32_t head = imuRingHead;
    uint32_t tail = imuRingTail;

    if (tail == head)
    {
        return false; // empty
    }

    sample = imuRingBuffer[tail];
    
    // Memory barrier: ensure sample is read before updating tail
    // On ESP32, volatile write provides release semantics
    imuRingTail = (tail + 1U) & IMU_RING_BUFFER_MASK;
    return true;
}

size_t imuGetBufferedSampleCount()
{
    // Use atomic reads and proper modulo arithmetic to handle wrap-around
    // This prevents integer overflow issues
    uint32_t head = imuRingHead;
    uint32_t tail = imuRingTail;
    
    // Use modulo arithmetic to handle wrap-around correctly
    // This is safe because buffer size is power of 2
    return (head - tail) & IMU_RING_BUFFER_MASK;
}

size_t imuGetOverflowCount()
{
    return imuOverflowCount;
}

// --- IMU init & single-read API ---

bool imuInit(TwoWire &wire)
{
    // Assume Wire.begin(...) has already been called with correct pins.
    (void)wire;

    if (!g_imu.begin())
    {
        // If needed, try alternate address here, e.g. 0x6A
        // if (!g_imu.begin(0x6A)) { ... }
        return false;
    }

    g_imu.deviceReset();
    while (!g_imu.getDeviceReset())
    {
        delay(1);
    }

    g_imu.enableBlockDataUpdate();

    // High-rate configuration:
    //   - Accel:  ±16 g,  960 Hz ODR
    //   - Gyro:   2000 dps, 960 Hz ODR
    g_imu.setAccelDataRate(LSM6DSV16X_ODR_AT_960Hz);
    g_imu.setAccelFullScale(LSM6DSV16X_16g);

    g_imu.setGyroDataRate(LSM6DSV16X_ODR_AT_960Hz);
    g_imu.setGyroFullScale(LSM6DSV16X_2000dps);

    // Filters
    g_imu.enableFilterSettling();

    g_imu.enableAccelLP2Filter();
    g_imu.setAccelLP2Bandwidth(LSM6DSV16X_XL_STRONG);

    g_imu.enableGyroLP1Filter();
    g_imu.setGyroLP1Bandwidth(LSM6DSV16X_GY_MEDIUM);

    // Interrupt pins prepared for possible future DRDY use.
    pinMode(IMU_INT1_PIN, INPUT);
    pinMode(IMU_INT2_PIN, INPUT);
    // attachInterrupt(digitalPinToInterrupt(IMU_INT1_PIN), imuInt1ISR, RISING);
    // attachInterrupt(digitalPinToInterrupt(IMU_INT2_PIN), imuInt2ISR, RISING);

    return true;
}

bool imuRead(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz)
{
    sfe_lsm_data_t accelData;
    sfe_lsm_data_t gyroData;

    if (!g_imu.checkStatus())
    {
        return false;
    }

    if (!g_imu.getAccel(&accelData))
        return false;
    if (!g_imu.getGyro(&gyroData))
        return false;

    ax = accelData.xData;
    ay = accelData.yData;
    az = accelData.zData;

    gx = gyroData.xData;
    gy = gyroData.yData;
    gz = gyroData.zData;

    return true;
}

// --- IMU sampling task ---

static void imuSamplingTask(void *param)
{
    (void)param;
    
    // Add this task to watchdog timer
    esp_task_wdt_add(NULL);

    sfe_lsm_data_t accelData;
    sfe_lsm_data_t gyroData;

    const TickType_t idleDelayTicks = pdMS_TO_TICKS(1); // ~1 ms

    // Watchdog reset counter (reset every ~1 second)
    uint32_t lastWdtReset = 0;

    for (;;)
    {
        // Reset watchdog timer periodically (every ~1 second)
        uint32_t now = millis();
        if (now - lastWdtReset > 1000)
        {
            esp_task_wdt_reset();
            lastWdtReset = now;
        }
        if (g_imu.checkStatus())
        {
            if (g_imu.getAccel(&accelData) && g_imu.getGyro(&gyroData))
            {
                ImuSample s;
                s.index = imuSampleIndexCounter;
                imuSampleIndexCounter = imuSampleIndexCounter + 1;
                s.adcSampleIndex = adcGetSampleCounter();

                s.ax = accelData.xData;
                s.ay = accelData.yData;
                s.az = accelData.zData;

                s.gx = gyroData.xData;
                s.gy = gyroData.yData;
                s.gz = gyroData.zData;

                imuRingPush(s);
            }
        }
        else
        {
            // No new data yet; yield a bit
            vTaskDelay(idleDelayTicks);
        }
    }
}

bool imuStartSamplingTask(UBaseType_t coreId)
{
    if (imuTaskHandle != nullptr)
    {
        return true; // already running
    }

    BaseType_t result = xTaskCreatePinnedToCore(
        imuSamplingTask,
        "ImuSampling",
        4096, // stack (adjust if needed)
        nullptr,
        configMAX_PRIORITIES - 2, // one step below ADC task
        &imuTaskHandle,
        coreId);
    
    if (result != pdPASS)
    {
        Serial.println("[IMU] ERROR: Failed to create IMU sampling task!");
        imuTaskHandle = nullptr;
        return false;
    }
    
    return true;
}
