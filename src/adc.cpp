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

bool adcOptimizeSettings(
    const AdcPgaGain *testGains, size_t numGains,
    const uint32_t *testRates, size_t numRates,
    size_t samplesPerTest,
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
    
    float minNoise = 1e9f;  // Large initial value
    AdcPgaGain bestGain = ADC_PGA_GAIN_4;
    uint32_t bestRate = 64000;
    
    Serial.println("[ADC_OPT] Starting optimization...");
    Serial.printf("[ADC_OPT] Mode: %s\n", 
                  mode == ADC_OPT_MODE_NOISE_ONLY ? "NOISE_ONLY" :
                  mode == ADC_OPT_MODE_SNR_SINGLE ? "SNR_SINGLE" :
                  "SNR_MULTIPOINT");
    Serial.printf("[ADC_OPT] Testing %d gains x %d rates = %d combinations\n",
                  gainCount, rateCount, gainCount * rateCount);
    
    if (mode == ADC_OPT_MODE_NOISE_ONLY)
    {
        Serial.println("[ADC_OPT] Ensure loadcell is at ZERO FORCE (unloaded)!");
    }
    else if (mode == ADC_OPT_MODE_SNR_SINGLE)
    {
        Serial.println("[ADC_OPT] Ensure known load is applied!");
        Serial.printf("[ADC_OPT] Baseline ADC: %ld\n", baselineAdc);
    }
    else if (mode == ADC_OPT_MODE_SNR_MULTIPOINT)
    {
        Serial.println("[ADC_OPT] Multi-point optimization with load points:");
        for (size_t i = 0; i < numLoadPoints; i++)
        {
            Serial.printf("[ADC_OPT]   Point %d: baseline=%ld, weight=%.2f, measured=%s\n",
                          i, loadPoints[i].baselineAdc, loadPoints[i].weight,
                          loadPoints[i].measured ? "YES" : "NO");
        }
    }
    
    float bestMetric = (mode == ADC_OPT_MODE_NOISE_ONLY) ? 1e9f : -1e9f;  // Min noise or max SNR
    
    // Test all combinations
    for (size_t g = 0; g < gainCount; g++)
    {
        for (size_t r = 0; r < rateCount; r++)
        {
            AdcPgaGain gain = gains[g];
            uint32_t rate = rates[r];
            
            Serial.printf("[ADC_OPT] Testing: Gain=x%d, Rate=%lu Hz... ",
                          adcPgaGainFactor(gain), rate);
            
            // Change settings
            if (!adcChangeSettings(gain, rate))
            {
                Serial.println("FAILED (settings change)");
                continue;
            }
            
            // Wait for settling (important after changing settings)
            delay(200);
            
            float metric = 0.0f;
            bool measurementOk = false;
            
            if (mode == ADC_OPT_MODE_NOISE_ONLY)
            {
                // Measure noise only
                float noise = 0.0f;
                if (adcMeasureNoise(samplesPerTest, noise, 10000))
                {
                    metric = noise;
                    measurementOk = true;
                    Serial.printf("Noise=%.2f ADC counts\n", noise);
                }
                else
                {
                    Serial.println("FAILED (noise measurement)");
                }
            }
            else if (mode == ADC_OPT_MODE_SNR_SINGLE)
            {
                // Measure SNR at single load point
                float signalRms = 0.0f, noiseRms = 0.0f, snrDb = 0.0f;
                if (adcMeasureSnr(samplesPerTest, baselineAdc, signalRms, noiseRms, snrDb, 10000))
                {
                    metric = snrDb;
                    measurementOk = true;
                    Serial.printf("SNR=%.2f dB (Signal=%.2f, Noise=%.2f)\n", snrDb, signalRms, noiseRms);
                }
                else
                {
                    Serial.println("FAILED (SNR measurement)");
                }
            }
            else if (mode == ADC_OPT_MODE_SNR_MULTIPOINT)
            {
                // Measure SNR at multiple load points and calculate weighted average
                float weightedSnr = 0.0f;
                float totalWeight = 0.0f;
                bool allPointsOk = true;
                
                for (size_t p = 0; p < numLoadPoints; p++)
                {
                    if (!loadPoints[p].measured)
                    {
                        continue;  // Skip unmeasured points
                    }
                    
                    float signalRms = 0.0f, noiseRms = 0.0f, snrDb = 0.0f;
                    if (adcMeasureSnr(samplesPerTest, loadPoints[p].baselineAdc, 
                                     signalRms, noiseRms, snrDb, 10000))
                    {
                        // Store results for this point
                        loadPoints[p].snrDb = snrDb;
                        loadPoints[p].signalRms = signalRms;
                        loadPoints[p].noiseRms = noiseRms;
                        
                        // Add to weighted sum
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
                    metric = weightedSnr / totalWeight;  // Weighted average SNR
                    measurementOk = true;
                    Serial.printf("Weighted SNR=%.2f dB (from %d points)\n", metric, numLoadPoints);
                }
                else
                {
                    Serial.println("FAILED (multi-point SNR measurement)");
                }
            }
            
            // Check if this is the best so far
            bool isBetter = false;
            if (mode == ADC_OPT_MODE_NOISE_ONLY)
            {
                isBetter = (metric < bestMetric);  // Lower noise is better
            }
            else
            {
                isBetter = (metric > bestMetric);  // Higher SNR is better
            }
            
            if (measurementOk && isBetter)
            {
                bestMetric = metric;
                bestGain = gain;
                bestRate = rate;
                
                if (mode == ADC_OPT_MODE_NOISE_ONLY)
                {
                    Serial.printf("[ADC_OPT] *** NEW BEST: Gain=x%d, Rate=%lu Hz, Noise=%.2f\n",
                                  adcPgaGainFactor(bestGain), bestRate, bestMetric);
                }
                else
                {
                    Serial.printf("[ADC_OPT] *** NEW BEST: Gain=x%d, Rate=%lu Hz, SNR=%.2f dB\n",
                                  adcPgaGainFactor(bestGain), bestRate, bestMetric);
                }
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
    
    result.success = true;
    
    return true;
}
