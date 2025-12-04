#include "adc.h"

#include <math.h>  // For sqrtf()
#include <stdlib.h>  // For malloc/free
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

// Use the default SPI instance from Arduino core
static SPIClass &adcSpi = SPI;

// --- Ring buffer ---

static const size_t ADC_RING_BUFFER_SIZE = 2048; // power of two
static const size_t ADC_RING_BUFFER_MASK = ADC_RING_BUFFER_SIZE - 1;

static AdcSample adcRingBuffer[ADC_RING_BUFFER_SIZE];
static volatile uint32_t adcRingHead = 0; // next write index
static volatile uint32_t adcRingTail = 0; // next read index
static volatile uint32_t adcSampleIndexCounter = 0;
static volatile uint32_t adcOverflowCount = 0;

static TaskHandle_t adcTaskHandle = nullptr;

// Push sample into ring buffer (called from sampling task)
static inline void adcRingPush(int32_t code)
{
    uint32_t head = adcRingHead;
    uint32_t nextHead = (head + 1U) & ADC_RING_BUFFER_MASK;
    uint32_t tail = adcRingTail;

    if (nextHead == tail)
    {
        // Buffer full, drop sample
        adcOverflowCount = adcOverflowCount + 1;
        return;
    }

    AdcSample &slot = adcRingBuffer[head];
    slot.index = adcSampleIndexCounter;
    adcSampleIndexCounter = adcSampleIndexCounter + 1;
    slot.code = code;

    adcRingHead = nextHead;
}

bool adcGetNextSample(AdcSample &sample)
{
    uint32_t tail = adcRingTail;
    uint32_t head = adcRingHead;

    if (tail == head)
    {
        return false; // empty
    }

    sample = adcRingBuffer[tail];
    adcRingTail = (tail + 1U) & ADC_RING_BUFFER_MASK;
    return true;
}

size_t adcGetBufferedSampleCount()
{
    uint32_t head = adcRingHead;
    uint32_t tail = adcRingTail;

    if (head >= tail)
        return head - tail;
    return (ADC_RING_BUFFER_SIZE - tail + head);
}

size_t adcGetOverflowCount()
{
    return adcOverflowCount;
}

uint32_t adcGetSampleCounter()
{
    return adcSampleIndexCounter;
}

// --- Internal helpers ---

static inline void adcSelect()
{
    digitalWrite(ADC_CS_PIN, LOW);
}

static inline void adcDeselect()
{
    digitalWrite(ADC_CS_PIN, HIGH);
}

// Build command byte for register mode (MODE = 1).
// START = 1, MODE = 1, RS[4:0] = reg, R/W in bit 0.
static uint8_t buildRegisterCommand(uint8_t reg, bool isRead)
{
    uint8_t cmd = 0;
    cmd |= 0x80;                                    // START = 1 (bit 7)
    cmd |= 0x40;                                    // MODE = 1 (bit 6, register mode)
    cmd |= static_cast<uint8_t>((reg & 0x1F) << 1); // RS4:0 bits 5:1
    if (isRead)
    {
        cmd |= 0x01; // R/W = 1 = read
    }
    return cmd;
}

// Build command byte for conversion mode (MODE = 0).
// START = 1, MODE = 0, CAL, IMPD, RATE3:0
static uint8_t buildConversionCommand(uint8_t rateCode, bool doCalibration, bool immediatePowerDown)
{
    uint8_t cmd = 0;
    cmd |= 0x80; // START = 1
    // MODE = 0, so bit 6 = 0
    if (doCalibration)
    {
        cmd |= 0x20; // CAL bit (B5)
    }
    if (immediatePowerDown)
    {
        cmd |= 0x10; // IMPD bit (B4)
    }
    cmd |= (rateCode & 0x0F); // RATE3:0 in B3..B0
    return cmd;
}

// Low-level register access
bool adcWriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t cmd = buildRegisterCommand(reg, false);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);
    adcSpi.transfer(value);
    adcSpi.endTransaction();
    adcDeselect();

    return true;
}

bool adcReadRegister(uint8_t reg, uint8_t &value)
{
    uint8_t cmd = buildRegisterCommand(reg, true);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);
    value = adcSpi.transfer(0x00);
    adcSpi.endTransaction();
    adcDeselect();

    return true;
}

// Read a 24-bit register (DATA and calibration registers)
static bool adcReadRegister24(uint8_t reg, uint32_t &raw)
{
    uint8_t cmd = buildRegisterCommand(reg, true);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);

    uint8_t b2 = adcSpi.transfer(0x00); // MSB
    uint8_t b1 = adcSpi.transfer(0x00);
    uint8_t b0 = adcSpi.transfer(0x00); // LSB

    adcSpi.endTransaction();
    adcDeselect();

    raw = (static_cast<uint32_t>(b2) << 16) |
          (static_cast<uint32_t>(b1) << 8) |
          (static_cast<uint32_t>(b0));

    return true;
}

// Convert 24-bit two's complement to signed 32-bit
static int32_t signExtend24(uint32_t raw24)
{
    if (raw24 & 0x800000UL)
    {
        raw24 |= 0xFF000000UL;
    }
    return static_cast<int32_t>(raw24);
}

// --- Public API: init, start, self-cal ---

bool adcInit(AdcPgaGain pgaGain)
{
    uint8_t pgaGainCode = static_cast<uint8_t>(pgaGain) & 0x07;

    // Configure GPIOs
    pinMode(ADC_CS_PIN, OUTPUT);
    pinMode(ADC_RSTB_PIN, OUTPUT);
    pinMode(ADC_SYNC_PIN, OUTPUT);
    pinMode(ADC_RDYB_PIN, INPUT_PULLUP); // RDYB is active-low output

    // Ensure bus is idle
    digitalWrite(ADC_CS_PIN, HIGH);
    digitalWrite(ADC_SYNC_PIN, HIGH); // SYNC idle high
    digitalWrite(ADC_RSTB_PIN, HIGH); // RSTB inactive (active low)

    // Initialize SPI with your pin mapping
    adcSpi.begin(ADC_SCK_PIN, ADC_MISO_PIN, ADC_MOSI_PIN, ADC_CS_PIN);

    // Hardware reset: pull RSTB low briefly then high
    digitalWrite(ADC_RSTB_PIN, LOW);
    delayMicroseconds(10); // t_RST min is small; 10 µs is safe
    digitalWrite(ADC_RSTB_PIN, HIGH);

    // Give the ADC some time to come out of reset
    delay(5);

    // Optional: clear any stale status by reading STAT
    uint8_t stat = 0;
    adcReadRegister(ADC_REG_STAT, stat);

    // CTRL1: continuous, bipolar, two's complement, internal clock
    uint8_t ctrl1 =
        (0 << 7) | // EXTCK
        (0 << 6) | // SYNCMODE
        (0 << 5) | // PD1
        (0 << 4) | // PD0
        (0 << 3) | // U/~B (0 = bipolar)
        (0 << 2) | // FORMAT (0 = two's complement)
        (0 << 1) | // SCYCLE (0 = continuous)
        (0 << 0);  // CONTSC (0 = normal continuous)

    adcWriteRegister(ADC_REG_CTRL1, ctrl1);

    // CTRL2: PGA enabled, gain set by pgaGainCode, no digital gain, normal power
    uint8_t ctrl2 =
        (0 << 7) |            // DGAIN1
        (0 << 6) |            // DGAIN0
        (0 << 5) |            // BUFEN
        (0 << 4) |            // LPMODE
        (1 << 3) |            // PGAEN
        (pgaGainCode & 0x07); // PGAG2:0

    adcWriteRegister(ADC_REG_CTRL2, ctrl2);

    // CTRL3, CTRL4, CTRL5: left at defaults; CTRL5 is used in adcSelfCalibrate().

    // Run self-calibration (offset + gain) once on init.
    if (!adcSelfCalibrate(0x0F, 500))
    {
        return false;
    }

    return true;
}

bool adcStartContinuous(uint8_t rateCode)
{
    uint8_t cmd = buildConversionCommand(rateCode & 0x0F, false, false);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);
    adcSpi.endTransaction();
    adcDeselect();

    // Give first conversion time to complete; at 64 ksps this is ~15.6 µs.
    delayMicroseconds(100);

    return true;
}

bool adcSelfCalibrate(uint8_t rateCode, uint32_t timeoutMs)
{
    // CTRL5: CAL1:CAL0 = bits B7:B6
    // Self-calibration: CAL[1:0] = 00
    uint8_t ctrl5 = 0;
    if (!adcReadRegister(ADC_REG_CTRL5, ctrl5))
    {
        return false;
    }

    ctrl5 &= ~(0xC0); // clear CAL1:CAL0
    if (!adcWriteRegister(ADC_REG_CTRL5, ctrl5))
    {
        return false;
    }

    // Issue calibration command (CAL bit = 1)
    uint8_t cmd = buildConversionCommand(rateCode & 0x0F, true, false);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);
    adcSpi.endTransaction();
    adcDeselect();

    // Wait for RDYB to assert (low) with timeout; self-cal ~200 ms.
    uint32_t startMs = millis();
    while (millis() - startMs < timeoutMs)
    {
        if (adcIsDataReady())
        {
            // One dummy read to clear RDYB and latch calib results internally
            int32_t dummy;
            adcReadSample(dummy);
            return true;
        }
        delay(1);
    }

    return false; // timeout
}

bool adcIsDataReady()
{
    // RDYB is active LOW: LOW = data ready
    return (digitalRead(ADC_RDYB_PIN) == LOW);
}

bool adcReadSample(int32_t &code)
{
    uint32_t raw24 = 0;
    if (!adcReadRegister24(ADC_REG_DATA, raw24))
    {
        return false;
    }

    code = signExtend24(raw24);
    return true;
}

// --- Sampling task ---

static void adcSamplingTask(void *param)
{
    (void)param;

    // Add this task to watchdog timer
    esp_task_wdt_add(NULL);

    // Explanation of ADC Busy Wait Issue:
    // When adcIsDataReady() returns false (no new sample ready), the task
    // immediately loops back and checks again without any delay. This creates
    // a "busy wait" that consumes 100% CPU on Core 0, wasting power and
    // potentially affecting other tasks on the same core (like IMU sampling).
    //
    // At 64 ksps, samples arrive every ~15.6 microseconds. When RDYB is high
    // (not ready), we should yield the CPU briefly rather than spinning.
    //
    // Solution: Use a very small delay (10 microseconds) when data is not ready.
    // This is:
    // - Much smaller than sample period (15.6µs), so we won't miss samples
    // - Large enough to yield CPU and prevent 100% usage
    // - The ring buffer (2048 samples) provides headroom for any timing variations
    //
    // IMPORTANT: The ADC hardware continues sampling at 64 ksps regardless of
    // our task timing. As long as we process samples faster than they arrive
    // on average, the 64 ksps rate is maintained. The ring buffer ensures no
    // samples are lost during brief delays.

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

        if (adcIsDataReady())
        {
            int32_t code;
            if (adcReadSample(code))
            {
                adcRingPush(code);
            }
            // RDYB will go high until the next conversion completes (~15.6µs)
            // No delay here - we want to check again immediately for next sample
            // This ensures we catch samples as soon as they're ready
        }
        else
        {
            // No data ready - yield CPU briefly to other tasks
            // 10µs delay is much smaller than sample period (15.6µs), so we
            // won't miss samples. The ring buffer provides safety margin.
            delayMicroseconds(10);
        }
    }
}

bool adcStartSamplingTask(UBaseType_t coreId)
{
    if (adcTaskHandle != nullptr)
    {
        return true; // already running
    }

    BaseType_t result = xTaskCreatePinnedToCore(
        adcSamplingTask,
        "AdcSampling",
        4096, // stack (adjust if needed)
        nullptr,
        configMAX_PRIORITIES - 1, // highest priority on that core
        &adcTaskHandle,
        coreId);
    
    if (result != pdPASS)
    {
        Serial.println("[ADC] ERROR: Failed to create ADC sampling task!");
        adcTaskHandle = nullptr;
        return false;
    }
    
    return true;
}

// ============================================================================
// CALIBRATION OPTIMIZATION FUNCTIONS
// ============================================================================

/**
 * @brief Convert sample rate in Hz to MAX11270 RATE code
 * @details MAX11270 RATE codes approximate: rate = 64e3 / (16 - code)
 */
static uint8_t sampleRateToRateCode(uint32_t sampleRateHz)
{
    // Common mappings for MAX11270
    if (sampleRateHz >= 64000) return 0x0F;  // 64 ksps
    if (sampleRateHz >= 32000) return 0x0E;  // 32 ksps
    if (sampleRateHz >= 16000) return 0x0D;  // 16 ksps
    if (sampleRateHz >= 8000)  return 0x0C;  // 8 ksps
    if (sampleRateHz >= 4000)  return 0x0B;  // 4 ksps
    if (sampleRateHz >= 2000)  return 0x0A;  // 2 ksps
    if (sampleRateHz >= 1000)  return 0x09;  // 1 ksps
    if (sampleRateHz >= 500)   return 0x08;  // 500 sps
    if (sampleRateHz >= 250)   return 0x07;  // 250 sps
    if (sampleRateHz >= 125)   return 0x06;  // 125 sps
    if (sampleRateHz >= 60)    return 0x05;  // 60 sps
    if (sampleRateHz >= 30)    return 0x04;  // 30 sps
    if (sampleRateHz >= 15)    return 0x03;  // 15 sps
    if (sampleRateHz >= 7)     return 0x02;  // 7.5 sps
    return 0x01;  // ~3.75 sps (minimum)
}

/**
 * @brief Stop the ADC sampling task temporarily
 */
static void adcStopSamplingTask()
{
    if (adcTaskHandle != nullptr)
    {
        vTaskDelete(adcTaskHandle);
        adcTaskHandle = nullptr;
        delay(50);  // Give task time to terminate
    }
}

bool adcChangeSettings(AdcPgaGain pgaGain, uint32_t sampleRate)
{
    uint8_t pgaGainCode = static_cast<uint8_t>(pgaGain) & 0x07;
    uint8_t rateCode = sampleRateToRateCode(sampleRate);
    
    // Update CTRL2 register for new PGA gain
    uint8_t ctrl2;
    if (!adcReadRegister(ADC_REG_CTRL2, ctrl2))
    {
        return false;
    }
    
    // Clear PGAG2:0 bits and set new gain
    ctrl2 &= ~0x07;  // Clear PGAG2:0
    ctrl2 |= (pgaGainCode & 0x07);
    
    if (!adcWriteRegister(ADC_REG_CTRL2, ctrl2))
    {
        return false;
    }
    
    // Wait for register write to complete
    delay(10);
    
    // Perform self-calibration with new settings
    if (!adcSelfCalibrate(rateCode, 500))
    {
        return false;
    }
    
    // Restart continuous conversion with new rate
    if (!adcStartContinuous(rateCode))
    {
        return false;
    }
    
    // Wait for ADC to settle (allow a few conversion cycles)
    delay(100);
    
    return true;
}

bool adcMeasureNoise(size_t numSamples, float &noiseStdDev, uint32_t timeoutMs)
{
    if (numSamples == 0 || numSamples > 100000)
    {
        return false;  // Sanity check
    }
    
    // Allocate array for samples (on stack if small, heap if large)
    int32_t *samples = nullptr;
    bool useHeap = (numSamples > 1000);
    
    if (useHeap)
    {
        samples = (int32_t*)malloc(numSamples * sizeof(int32_t));
        if (samples == nullptr)
        {
            return false;  // Out of memory
        }
    }
    else
    {
        // Use stack allocation for small arrays
        static int32_t stackSamples[1000];
        samples = stackSamples;
    }
    
    // Collect samples
    uint32_t startTime = millis();
    size_t collected = 0;
    
    while (collected < numSamples)
    {
        if (millis() - startTime > timeoutMs)
        {
            if (useHeap) free(samples);
            return false;  // Timeout
        }
        
        if (adcIsDataReady())
        {
            if (adcReadSample(samples[collected]))
            {
                collected++;
            }
        }
        else
        {
            delayMicroseconds(50);  // Small delay when not ready
        }
    }
    
    // Calculate mean
    int64_t sum = 0;
    for (size_t i = 0; i < numSamples; i++)
    {
        sum += samples[i];
    }
    float mean = static_cast<float>(sum) / static_cast<float>(numSamples);
    
    // Calculate variance
    float variance = 0.0f;
    for (size_t i = 0; i < numSamples; i++)
    {
        float diff = static_cast<float>(samples[i]) - mean;
        variance += diff * diff;
    }
    variance /= static_cast<float>(numSamples);
    
    // Standard deviation (noise level)
    noiseStdDev = sqrtf(variance);
    
    if (useHeap)
    {
        free(samples);
    }
    
    return true;
}

bool adcMeasureSnr(size_t numSamples, int32_t baselineAdc, 
                   float &signalRms, float &noiseRms, float &snrDb, 
                   uint32_t timeoutMs)
{
    if (numSamples == 0 || numSamples > 100000)
    {
        return false;  // Sanity check
    }
    
    // Allocate array for samples (on stack if small, heap if large)
    int32_t *samples = nullptr;
    bool useHeap = (numSamples > 1000);
    
    if (useHeap)
    {
        samples = (int32_t*)malloc(numSamples * sizeof(int32_t));
        if (samples == nullptr)
        {
            return false;  // Out of memory
        }
    }
    else
    {
        // Use stack allocation for small arrays
        static int32_t stackSamples[1000];
        samples = stackSamples;
    }
    
    // Collect samples
    uint32_t startTime = millis();
    size_t collected = 0;
    
    while (collected < numSamples)
    {
        if (millis() - startTime > timeoutMs)
        {
            if (useHeap) free(samples);
            return false;  // Timeout
        }
        
        if (adcIsDataReady())
        {
            if (adcReadSample(samples[collected]))
            {
                collected++;
            }
        }
        else
        {
            delayMicroseconds(50);  // Small delay when not ready
        }
    }
    
    // Calculate signal RMS (deviation from baseline)
    float signalVariance = 0.0f;
    float mean = 0.0f;
    
    // First pass: calculate mean
    int64_t sum = 0;
    for (size_t i = 0; i < numSamples; i++)
    {
        sum += samples[i];
    }
    mean = static_cast<float>(sum) / static_cast<float>(numSamples);
    
    // Second pass: calculate signal variance (deviation from baseline)
    for (size_t i = 0; i < numSamples; i++)
    {
        float deviation = static_cast<float>(samples[i]) - static_cast<float>(baselineAdc);
        signalVariance += deviation * deviation;
    }
    signalVariance /= static_cast<float>(numSamples);
    signalRms = sqrtf(signalVariance);
    
    // Calculate noise RMS (variation around mean)
    float noiseVariance = 0.0f;
    for (size_t i = 0; i < numSamples; i++)
    {
        float deviation = static_cast<float>(samples[i]) - mean;
        noiseVariance += deviation * deviation;
    }
    noiseVariance /= static_cast<float>(numSamples);
    noiseRms = sqrtf(noiseVariance);
    
    // Calculate SNR in dB
    // SNR = 20 * log10(Signal_RMS / Noise_RMS)
    if (noiseRms > 0.0f && signalRms > 0.0f)
    {
        snrDb = 20.0f * log10f(signalRms / noiseRms);
    }
    else
    {
        snrDb = -100.0f;  // Invalid SNR
    }
    
    if (useHeap)
    {
        free(samples);
    }
    
    return true;
}

// ============================================================================
// LOAD STABILITY DETECTION (Optimization #2)
// ============================================================================

bool adcCheckLoadStability(size_t numSamples, float stabilityThreshold, 
                          int32_t &stableValue, uint32_t timeoutMs)
{
    if (numSamples == 0 || numSamples > 10000)
    {
        return false;  // Sanity check
    }
    
    // Allocate array for samples
    int32_t *samples = nullptr;
    bool useHeap = (numSamples > 500);
    
    if (useHeap)
    {
        samples = (int32_t*)malloc(numSamples * sizeof(int32_t));
        if (samples == nullptr)
        {
            return false;
        }
    }
    else
    {
        static int32_t stackSamples[500];
        samples = stackSamples;
    }
    
    // Collect samples
    uint32_t startTime = millis();
    size_t collected = 0;
    
    while (collected < numSamples)
    {
        if (millis() - startTime > timeoutMs)
        {
            if (useHeap) free(samples);
            return false;  // Timeout
        }
        
        if (adcIsDataReady())
        {
            if (adcReadSample(samples[collected]))
            {
                collected++;
            }
        }
        else
        {
            delayMicroseconds(50);
        }
    }
    
    // Calculate mean
    int64_t sum = 0;
    for (size_t i = 0; i < numSamples; i++)
    {
        sum += samples[i];
    }
    float mean = static_cast<float>(sum) / static_cast<float>(numSamples);
    stableValue = static_cast<int32_t>(mean);
    
    // Calculate variance
    float variance = 0.0f;
    for (size_t i = 0; i < numSamples; i++)
    {
        float diff = static_cast<float>(samples[i]) - mean;
        variance += diff * diff;
    }
    variance /= static_cast<float>(numSamples);
    
    if (useHeap)
    {
        free(samples);
    }
    
    // Check if variance is below threshold (stable)
    return (variance <= stabilityThreshold);
}

// ============================================================================
// LOAD POINT VALIDATION (Optimization #9)
// ============================================================================

bool adcValidateLoadPoints(const AdcLoadPoint *loadPoints, size_t numLoadPoints,
                          const char **warnings, size_t maxWarnings, size_t &numWarnings)
{
    numWarnings = 0;
    if (loadPoints == nullptr || numLoadPoints == 0)
    {
        return false;
    }
    
    bool isValid = true;
    
    // Check 1: All points should be measured
    for (size_t i = 0; i < numLoadPoints; i++)
    {
        if (!loadPoints[i].measured && numWarnings < maxWarnings && warnings)
        {
            warnings[numWarnings++] = "Load point not measured";
            isValid = false;
        }
    }
    
    // Check 2: Load points should be in order (increasing baseline for loading)
    for (size_t i = 1; i < numLoadPoints; i++)
    {
        if (loadPoints[i].baselineAdc < loadPoints[i-1].baselineAdc && 
            numWarnings < maxWarnings && warnings)
        {
            warnings[numWarnings++] = "Load points not in increasing order";
            isValid = false;
        }
    }
    
    // Check 3: SNR values should be reasonable (typically 20-80 dB)
    for (size_t i = 0; i < numLoadPoints; i++)
    {
        if (loadPoints[i].measured)
        {
            if (loadPoints[i].snrDb < 10.0f && numWarnings < maxWarnings && warnings)
            {
                warnings[numWarnings++] = "Very low SNR detected (check connections)";
            }
            if (loadPoints[i].snrDb > 100.0f && numWarnings < maxWarnings && warnings)
            {
                warnings[numWarnings++] = "Unusually high SNR (verify measurement)";
            }
        }
    }
    
    // Check 4: Weights should sum to approximately 1.0
    float totalWeight = 0.0f;
    for (size_t i = 0; i < numLoadPoints; i++)
    {
        totalWeight += loadPoints[i].weight;
    }
    if (fabsf(totalWeight - 1.0f) > 0.1f && numWarnings < maxWarnings && warnings)
    {
        warnings[numWarnings++] = "Load point weights don't sum to 1.0";
    }
    
    // Check 5: SNR should generally increase with load (for most loadcells)
    int increasingCount = 0;
    for (size_t i = 1; i < numLoadPoints; i++)
    {
        if (loadPoints[i].measured && loadPoints[i-1].measured)
        {
            if (loadPoints[i].snrDb > loadPoints[i-1].snrDb)
            {
                increasingCount++;
            }
        }
    }
    if (increasingCount < numLoadPoints / 2 && numWarnings < maxWarnings && warnings)
    {
        warnings[numWarnings++] = "SNR not increasing with load (may indicate issues)";
    }
    
    return isValid;
}

// ============================================================================
// AUTO-DETECT LOAD POINTS (Optimization #8)
// ============================================================================

bool adcAutoDetectLoadPoint(int32_t previousAdc, int32_t changeThreshold,
                           float stabilityThreshold, int32_t &detectedAdc,
                           uint32_t timeoutMs)
{
    uint32_t startTime = millis();
    const size_t CHECK_SAMPLES = 100;
    int32_t lastStableValue = previousAdc;
    
    while (millis() - startTime < timeoutMs)
    {
        // Check if load has changed significantly
        int32_t currentAdc = 0;
        if (adcCheckLoadStability(CHECK_SAMPLES, stabilityThreshold, currentAdc, 2000))
        {
            int32_t change = abs(currentAdc - previousAdc);
            
            if (change >= changeThreshold)
            {
                // Load has changed - verify it's stable at new value
                int32_t stableAdc = 0;
                if (adcCheckLoadStability(CHECK_SAMPLES * 2, stabilityThreshold, stableAdc, 3000))
                {
                    // Verify it's still at the new level
                    if (abs(stableAdc - currentAdc) < changeThreshold / 2)
                    {
                        detectedAdc = stableAdc;
                        return true;
                    }
                }
            }
            
            lastStableValue = currentAdc;
        }
        
        delay(100);  // Check every 100ms
    }
    
    // Timeout - return last stable value
    detectedAdc = lastStableValue;
    return false;
}

// ============================================================================
// OPTIMIZATION WITH MULTIPLE SEARCH STRATEGIES
// ============================================================================

// Helper function to test a single combination and return metric
static float testCombination(
    AdcOptimizationMode mode,
    AdcPgaGain gain,
    uint32_t rate,
    size_t samplesPerTest,
    int32_t baselineAdc,
    AdcLoadPoint *loadPoints,
    size_t numLoadPoints,
    bool &success)
{
    success = false;
    
    // Change settings
    if (!adcChangeSettings(gain, rate))
    {
        return 0.0f;
    }
    
    // Wait for settling
    delay(200);
    
    float metric = 0.0f;
    
    if (mode == ADC_OPT_MODE_NOISE_ONLY)
    {
        float noise = 0.0f;
        if (adcMeasureNoise(samplesPerTest, noise, 10000))
        {
            metric = noise;
            success = true;
        }
    }
    else if (mode == ADC_OPT_MODE_SNR_SINGLE)
    {
        float signalRms = 0.0f, noiseRms = 0.0f, snrDb = 0.0f;
        if (adcMeasureSnr(samplesPerTest, baselineAdc, signalRms, noiseRms, snrDb, 10000))
        {
            metric = snrDb;
            success = true;
        }
    }
    else if (mode == ADC_OPT_MODE_SNR_MULTIPOINT)
    {
        float weightedSnr = 0.0f;
        float totalWeight = 0.0f;
        bool allPointsOk = true;
        
        for (size_t p = 0; p < numLoadPoints; p++)
        {
            if (!loadPoints[p].measured)
            {
                continue;
            }
            
            float signalRms = 0.0f, noiseRms = 0.0f, snrDb = 0.0f;
            if (adcMeasureSnr(samplesPerTest, loadPoints[p].baselineAdc, 
                             signalRms, noiseRms, snrDb, 10000))
            {
                loadPoints[p].snrDb = snrDb;
                loadPoints[p].signalRms = signalRms;
                loadPoints[p].noiseRms = noiseRms;
                weightedSnr += snrDb * loadPoints[p].weight;
                totalWeight += loadPoints[p].weight;
            }
            else
            {
                allPointsOk = false;
                break;
            }
        }
        
        if (allPointsOk && totalWeight > 0.0f)
        {
            metric = weightedSnr / totalWeight;
            success = true;
        }
    }
    
    return metric;
}

bool adcOptimizeSettings(
    AdcOptimizationMode mode,
    AdcSearchStrategy strategy,
    const AdcPgaGain *testGains, size_t numGains,
    const uint32_t *testRates, size_t numRates,
    size_t samplesPerTest,
    int32_t baselineAdc,
    AdcLoadPoint *loadPoints,
    size_t numLoadPoints,
    AdcOptimizationProgressCallback progressCallback,
    AdcOptimizationResult &result)
{
    result.success = false;
    
    // Default test gains if not provided
    static const AdcPgaGain defaultGains[] = {
        ADC_PGA_GAIN_1, ADC_PGA_GAIN_2, ADC_PGA_GAIN_4, ADC_PGA_GAIN_8,
        ADC_PGA_GAIN_16, ADC_PGA_GAIN_32, ADC_PGA_GAIN_64, ADC_PGA_GAIN_128
    };
    
    // Default test rates (Hz) - common values
    static const uint32_t defaultRates[] = {
        1000, 2000, 4000, 8000, 16000, 32000, 64000
    };
    
    const AdcPgaGain *gains = (testGains && numGains > 0) ? testGains : defaultGains;
    size_t gainCount = (testGains && numGains > 0) ? numGains : 8;
    
    const uint32_t *rates = (testRates && numRates > 0) ? testRates : defaultRates;
    size_t rateCount = (testRates && numRates > 0) ? numRates : 7;
    
    // Stop sampling task to prevent interference
    adcStopSamplingTask();
    
    // Initialize best values
    float bestMetric = (mode == ADC_OPT_MODE_NOISE_ONLY) ? 1e9f : -1e9f;
    AdcPgaGain bestGain = ADC_PGA_GAIN_4;
    uint32_t bestRate = 64000;
    size_t bestGainIdx = 2;  // Default x4
    size_t bestRateIdx = 6;  // Default 64kHz
    
    Serial.println("[ADC_OPT] Starting optimization...");
    Serial.printf("[ADC_OPT] Mode: %s, Strategy: %s\n", 
                  mode == ADC_OPT_MODE_NOISE_ONLY ? "NOISE_ONLY" :
                  mode == ADC_OPT_MODE_SNR_SINGLE ? "SNR_SINGLE" :
                  "SNR_MULTIPOINT",
                  strategy == ADC_SEARCH_EXHAUSTIVE ? "EXHAUSTIVE" :
                  strategy == ADC_SEARCH_ADAPTIVE ? "ADAPTIVE" :
                  "GRADIENT");
    
    if (progressCallback)
    {
        char status[64];
        snprintf(status, sizeof(status), "Starting optimization...");
        progressCallback(0, 100, status);
    }
    
    // ========================================================================
    // ADAPTIVE SEARCH STRATEGY (Optimization #1)
    // ========================================================================
    if (strategy == ADC_SEARCH_ADAPTIVE)
    {
        Serial.println("[ADC_OPT] Using ADAPTIVE search (coarse then fine)");
        
        // Phase 1: Coarse search (every 2nd gain, every 2nd rate)
        size_t coarseTotal = ((gainCount + 1) / 2) * ((rateCount + 1) / 2);
        size_t coarseCurrent = 0;
        
        Serial.printf("[ADC_OPT] Phase 1 (Coarse): Testing %d combinations\n", coarseTotal);
        
        for (size_t g = 0; g < gainCount; g += 2)  // Every 2nd gain
        {
            for (size_t r = 0; r < rateCount; r += 2)  // Every 2nd rate
            {
                AdcPgaGain gain = gains[g];
                uint32_t rate = rates[r];
                
                if (progressCallback)
                {
                    char status[128];
                    snprintf(status, sizeof(status), "Coarse: Gain=x%d, Rate=%lu Hz", 
                            adcPgaGainFactor(gain), rate);
                    progressCallback(coarseCurrent, coarseTotal, status);
                }
                
                bool success = false;
                float metric = testCombination(mode, gain, rate, samplesPerTest, 
                                              baselineAdc, loadPoints, numLoadPoints, success);
                
                if (success)
                {
                    bool isBetter = (mode == ADC_OPT_MODE_NOISE_ONLY) ? 
                                   (metric < bestMetric) : (metric > bestMetric);
                    
                    if (isBetter)
                    {
                        bestMetric = metric;
                        bestGain = gain;
                        bestRate = rate;
                        bestGainIdx = g;
                        bestRateIdx = r;
                        
                        Serial.printf("[ADC_OPT] *** NEW BEST (Coarse): Gain=x%d, Rate=%lu Hz\n",
                                      adcPgaGainFactor(bestGain), bestRate);
                    }
                }
                
                coarseCurrent++;
            }
        }
        
        // Phase 2: Fine search around best point (±1 gain, ±1 rate)
        Serial.printf("[ADC_OPT] Phase 2 (Fine): Refining around Gain=x%d, Rate=%lu Hz\n",
                      adcPgaGainFactor(bestGain), bestRate);
        
        size_t fineTotal = 9;  // 3x3 grid
        size_t fineCurrent = 0;
        
        for (int gOffset = -1; gOffset <= 1; gOffset++)
        {
            int g = static_cast<int>(bestGainIdx) + gOffset;
            if (g < 0 || g >= static_cast<int>(gainCount)) continue;
            
            for (int rOffset = -1; rOffset <= 1; rOffset++)
            {
                int r = static_cast<int>(bestRateIdx) + rOffset;
                if (r < 0 || r >= static_cast<int>(rateCount)) continue;
                
                // Skip if we already tested this in coarse phase
                if ((g % 2 == 0) && (r % 2 == 0)) continue;
                
                AdcPgaGain gain = gains[g];
                uint32_t rate = rates[r];
                
                if (progressCallback)
                {
                    char status[128];
                    snprintf(status, sizeof(status), "Fine: Gain=x%d, Rate=%lu Hz", 
                            adcPgaGainFactor(gain), rate);
                    progressCallback(coarseTotal + fineCurrent, coarseTotal + fineTotal, status);
                }
                
                bool success = false;
                float metric = testCombination(mode, gain, rate, samplesPerTest, 
                                              baselineAdc, loadPoints, numLoadPoints, success);
                
                if (success)
                {
                    bool isBetter = (mode == ADC_OPT_MODE_NOISE_ONLY) ? 
                                   (metric < bestMetric) : (metric > bestMetric);
                    
                    if (isBetter)
                    {
                        bestMetric = metric;
                        bestGain = gain;
                        bestRate = rate;
                        
                        Serial.printf("[ADC_OPT] *** NEW BEST (Fine): Gain=x%d, Rate=%lu Hz\n",
                                      adcPgaGainFactor(bestGain), bestRate);
                    }
                }
                
                fineCurrent++;
            }
        }
        
        Serial.printf("[ADC_OPT] Adaptive search complete: %d total tests (vs %d exhaustive)\n",
                      coarseTotal + fineTotal, gainCount * rateCount);
    }
    // ========================================================================
    // GRADIENT-BASED SEARCH (Optimization #10)
    // ========================================================================
    else if (strategy == ADC_SEARCH_GRADIENT)
    {
        Serial.println("[ADC_OPT] Using GRADIENT search");
        
        // Start from reasonable initial guess (middle of range)
        size_t currentG = gainCount / 2;
        size_t currentR = rateCount / 2;
        
        const float stepSize = 0.5f;  // Step size for gradient
        const size_t maxIterations = 20;
        
        for (size_t iter = 0; iter < maxIterations; iter++)
        {
            AdcPgaGain currentGain = gains[currentG];
            uint32_t currentRate = rates[currentR];
            
            if (progressCallback)
            {
                char status[128];
                snprintf(status, sizeof(status), "Gradient iter %d/%d: Gain=x%d, Rate=%lu Hz", 
                        iter + 1, maxIterations, adcPgaGainFactor(currentGain), currentRate);
                progressCallback(iter, maxIterations, status);
            }
            
            // Measure current point
            bool success = false;
            float currentMetric = testCombination(mode, currentGain, currentRate, samplesPerTest,
                                                 baselineAdc, loadPoints, numLoadPoints, success);
            
            if (!success) break;
            
            // Test neighbors to estimate gradient
            float bestNeighborMetric = currentMetric;
            int bestGOffset = 0;
            int bestROffset = 0;
            
            for (int gOffset = -1; gOffset <= 1; gOffset++)
            {
                for (int rOffset = -1; rOffset <= 1; rOffset++)
                {
                    if (gOffset == 0 && rOffset == 0) continue;
                    
                    int g = static_cast<int>(currentG) + gOffset;
                    int r = static_cast<int>(currentR) + rOffset;
                    
                    if (g < 0 || g >= static_cast<int>(gainCount)) continue;
                    if (r < 0 || r >= static_cast<int>(rateCount)) continue;
                    
                    AdcPgaGain testGain = gains[g];
                    uint32_t testRate = rates[r];
                    
                    bool neighborSuccess = false;
                    float neighborMetric = testCombination(mode, testGain, testRate, samplesPerTest,
                                                          baselineAdc, loadPoints, numLoadPoints, neighborSuccess);
                    
                    if (neighborSuccess)
                    {
                        bool isBetter = (mode == ADC_OPT_MODE_NOISE_ONLY) ?
                                       (neighborMetric < bestNeighborMetric) :
                                       (neighborMetric > bestNeighborMetric);
                        
                        if (isBetter)
                        {
                            bestNeighborMetric = neighborMetric;
                            bestGOffset = gOffset;
                            bestROffset = rOffset;
                        }
                    }
                }
            }
            
            // If no better neighbor found, we're at a local optimum
            if (bestGOffset == 0 && bestROffset == 0)
            {
                Serial.println("[ADC_OPT] Local optimum found, stopping gradient search");
                bestGain = currentGain;
                bestRate = currentRate;
                bestMetric = currentMetric;
                break;
            }
            
            // Move in direction of best neighbor
            currentG = static_cast<size_t>(static_cast<int>(currentG) + bestGOffset);
            currentR = static_cast<size_t>(static_cast<int>(currentR) + bestROffset);
            
            // Update best if this is better
            bool isBetter = (mode == ADC_OPT_MODE_NOISE_ONLY) ?
                           (bestNeighborMetric < bestMetric) :
                           (bestNeighborMetric > bestMetric);
            
            if (isBetter)
            {
                bestMetric = bestNeighborMetric;
                bestGain = gains[currentG];
                bestRate = rates[currentR];
            }
        }
    }
    // ========================================================================
    // EXHAUSTIVE SEARCH (Default)
    // ========================================================================
    else  // ADC_SEARCH_EXHAUSTIVE
    {
        Serial.printf("[ADC_OPT] Using EXHAUSTIVE search: Testing %d combinations\n",
                      gainCount * rateCount);
        
        size_t totalCombinations = gainCount * rateCount;
        size_t current = 0;
        
        for (size_t g = 0; g < gainCount; g++)
        {
            for (size_t r = 0; r < rateCount; r++)
            {
                AdcPgaGain gain = gains[g];
                uint32_t rate = rates[r];
                
                if (progressCallback)
                {
                    char status[128];
                    snprintf(status, sizeof(status), "Testing: Gain=x%d, Rate=%lu Hz", 
                            adcPgaGainFactor(gain), rate);
                    progressCallback(current, totalCombinations, status);
                }
                
                bool success = false;
                float metric = testCombination(mode, gain, rate, samplesPerTest, 
                                              baselineAdc, loadPoints, numLoadPoints, success);
                
                if (success)
                {
                    bool isBetter = (mode == ADC_OPT_MODE_NOISE_ONLY) ? 
                                   (metric < bestMetric) : (metric > bestMetric);
                    
                    if (isBetter)
                    {
                        bestMetric = metric;
                        bestGain = gain;
                        bestRate = rate;
                        
                        Serial.printf("[ADC_OPT] *** NEW BEST: Gain=x%d, Rate=%lu Hz\n",
                                      adcPgaGainFactor(bestGain), bestRate);
                    }
                }
                
                current++;
            }
        }
    }
    
    // Set optimal settings
    Serial.println("[ADC_OPT] Setting optimal configuration...");
    if (!adcChangeSettings(bestGain, bestRate))
    {
        Serial.println("[ADC_OPT] ERROR: Failed to set optimal settings!");
        return false;
    }
    
    // Populate result
    result.optimalGain = bestGain;
    result.optimalSampleRate = bestRate;
    
    if (mode == ADC_OPT_MODE_NOISE_ONLY)
    {
        result.noiseLevel = bestMetric;
        result.snrDb = 0.0f;
        Serial.println("[ADC_OPT] Optimization complete!");
        Serial.printf("[ADC_OPT] Optimal: Gain=x%d, Rate=%lu Hz, Noise=%.2f ADC counts\n",
                      adcPgaGainFactor(bestGain), bestRate, bestMetric);
    }
    else
    {
        result.noiseLevel = 0.0f;
        result.snrDb = bestMetric;
        Serial.println("[ADC_OPT] Optimization complete!");
        Serial.printf("[ADC_OPT] Optimal: Gain=x%d, Rate=%lu Hz, SNR=%.2f dB\n",
                      adcPgaGainFactor(bestGain), bestRate, bestMetric);
    }
    
    if (progressCallback)
    {
        char status[128];
        snprintf(status, sizeof(status), "Complete: Gain=x%d, Rate=%lu Hz", 
                adcPgaGainFactor(bestGain), bestRate);
        progressCallback(100, 100, status);
    }
    
    result.success = true;
    return true;
}
