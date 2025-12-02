#include "imu.h"
#include <SparkFun_LSM6DSV16X.h>

// Global instance
SparkFun_LSM6DSV16X g_imu;

// Simple interrupt flags (if you want to use INTs later)
volatile bool g_imuInt1Fired = false;
volatile bool g_imuInt2Fired = false;

static void IRAM_ATTR imuInt1ISR() {
    g_imuInt1Fired = true;
}

static void IRAM_ATTR imuInt2ISR() {
    g_imuInt2Fired = true;
}

bool imu_init(TwoWire &wire)
{
    // Configure I2C pins *before* calling Wire.begin()
    // If Wire.begin() is already called elsewhere, you can remove this call
    // or make sure it's consistent with your I2C pin choices.
    // Example (matches your pinout: SDA=41, SCL=42):
    wire.begin(41, 42);  // SDA, SCL

    // Start serial debug only if you want to see debug output here
    // Serial.begin(115200);

    if (!g_imu.begin())
    {
        // Serial.println("IMU: begin() failed. Check wiring / power.");
        return false;
    }

    // Make sure Block Data Update is enabled (prevents partial reads)
    g_imu.enableBlockDataUpdate();

    // Configure accelerometer:
    //  - Data rate: e.g. 120 Hz (good for general logging)
    //  - Full scale: 16g (per your earlier derating decision)
    g_imu.setAccelDataRate(LSM6DSV16X_ODR_AT_120Hz);
    g_imu.setAccelFullScale(LSM6DSV16X_16g);

    // Configure gyroscope:
    //  - Data rate: 120 Hz
    //  - Full scale: 2000 dps (typical)
    g_imu.setGyroDataRate(LSM6DSV16X_ODR_AT_120Hz);
    g_imu.setGyroFullScale(LSM6DSV16X_2000dps);

    // Enable filter settling and basic filters for cleaner data
    g_imu.enableFilterSettling();
    g_imu.enableAccelLP2Filter();
    g_imu.setAccelLP2Bandwidth(LSM6DSV16X_XL_STRONG);

    // OPTIONAL: route data-ready interrupt to INT1
    // (You can skip this if you just poll in your main loop for now.)
    g_imu.setAccelStatusInterrupt(true);
    g_imu.int1RouteAccelDataReady(true);
    // g_imu.int2RouteAccelDataReady(true); // if you prefer INT2

    // Setup GPIO interrupts on ESP32 for INT1/INT2
    pinMode(IMU_INT1_PIN, INPUT);
    attachInterrupt(IMU_INT1_PIN, imuInt1ISR, RISING);

    pinMode(IMU_INT2_PIN, INPUT);
    attachInterrupt(IMU_INT2_PIN, imuInt2ISR, RISING);

    // Clear any latched status by doing an initial read
    sfe_lsm_data_t accelData, gyroData;
    g_imu.getAccel(&accelData);
    g_imu.getGyro(&gyroData);

    return true;
}

bool imu_read(float &ax, float &ay, float &az,
              float &gx, float &gy, float &gz)
{
    sfe_lsm_data_t accelData;
    sfe_lsm_data_t gyroData;

    if (!g_imu.checkStatus())
    {
        // No new data available yet
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
