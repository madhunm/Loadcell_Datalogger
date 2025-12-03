#include <Arduino.h>
#include <Wire.h>

#include "pins.h"
#include "sdcard.h"
#include "rtc.h"
#include "imu.h"
#include "neopixel.h"
#include "adc.h"

// ---- System state machine ----
enum SystemState
{
    STATE_INIT = 0,
    STATE_READY,
    STATE_LOGGING
};

SystemState systemState = STATE_INIT;

// Flag to ensure we start sampling tasks only once
static bool samplingTasksStarted = false;

// ---- Combined peripheral init ----
bool initPeripherals()
{
    // Try each peripheral in turn; on failure, select a per-peripheral
    // error pattern and return false immediately.

    if (!sdCardInit())
    {
        Serial.println("[INIT] SD card initialisation failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
        return false;
    }

    if (!imuInit(Wire))
    {
        Serial.println("[INIT] IMU initialisation failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_IMU);
        return false;
    }

    if (!rtcInit())
    {
        Serial.println("[INIT] RTC initialisation failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_RTC);
        return false;
    }

    // ADC Gains available (from AdcPgaGain):
    //  ADC_PGA_GAIN_1
    //  ADC_PGA_GAIN_2
    //  ADC_PGA_GAIN_4
    //  ADC_PGA_GAIN_8
    //  ADC_PGA_GAIN_16
    //  ADC_PGA_GAIN_32
    //  ADC_PGA_GAIN_64
    //  ADC_PGA_GAIN_128

    // For bring-up, choose a safe gain like x4 or x8
    AdcPgaGain gain = ADC_PGA_GAIN_4;

    if (!adcInit(gain))
    {
        Serial.println("[INIT][ADC] adcInit() (including self-cal) failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_ADC);
        return false;
    }

    // Start continuous conversion at 64 ksps (RATE code 0x0F)
    if (!adcStartContinuous(0x0F))
    {
        Serial.println("[INIT][ADC] adcStartContinuous() failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_ADC);
        return false;
    }

    return true;
}

// ---- Logging stub ----
// For now this just prints a heartbeat; later we will:
//  - Drain adcGetNextSample()
//  - Drain imuGetNextSample()
//  - Write to SD
void loggingTick()
{
    static uint32_t lastPrint = 0;
    uint32_t now = millis();
    if (now - lastPrint > 1000)
    {
        lastPrint = now;

        size_t adcCount = adcGetBufferedSampleCount();
        size_t adcOvfl = adcGetOverflowCount();
        size_t imuCount = imuGetBufferedSampleCount();
        size_t imuOvfl = imuGetOverflowCount();

        Serial.printf("[LOG] tick: ADC buffered=%u ovfl=%u, IMU buffered=%u ovfl=%u\n",
                      (unsigned)adcCount, (unsigned)adcOvfl,
                      (unsigned)imuCount, (unsigned)imuOvfl);
    }
}

void setup()
{
    delay(500);

    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("Loadcell Logger – ESP32-S3 bring-up");
    Serial.println("------------------------------------");

    // ---- GPIO directions ----

    // Logstart button: external pulldown (LOW idle, HIGH pressed)
    pinMode(PIN_LOGSTART_BUTTON, INPUT);

    // ADC SPI pins (redundant with adcInit, but harmless)
    pinMode(PIN_ADC_CS, OUTPUT);
    pinMode(PIN_ADC_SCK, OUTPUT);
    pinMode(PIN_ADC_MOSI, OUTPUT);
    pinMode(PIN_ADC_MISO, INPUT);

    pinMode(PIN_ADC_SYNC, OUTPUT);
    pinMode(PIN_ADC_RSTB, OUTPUT);
    pinMode(PIN_ADC_RDYB, INPUT);

    // RTC pins
    pinMode(PIN_RTC_FOUT, INPUT); // FOUT routed but not used yet

    // IMU interrupt pins as inputs (IMU module also sets them)
    pinMode(PIN_IMU_INT1, INPUT);
    pinMode(PIN_IMU_INT2, INPUT);

    // I2C bus for RTC + IMU + gauge, etc.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.printf("I2C started on SDA=%d SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);

    // NeoPixel
    neopixelInit();
    neopixelSetPattern(NEOPIXEL_PATTERN_INIT);

    // Peripheral initialisation
    systemState = STATE_INIT;
    Serial.println("[STATE] INIT: bringing up peripherals...");

    bool ok = initPeripherals();
    if (ok)
    {
        systemState = STATE_READY;
        neopixelSetPattern(NEOPIXEL_PATTERN_READY);
        Serial.println("[STATE] READY: system ready to log (waiting for Logstart button)");
    }
    else
    {
        systemState = STATE_INIT;
        // Pattern already set by initPeripherals() to specific error.
        Serial.println("[ERROR] Peripheral init failed. Error pattern set.");
    }
}

void loop()
{
    // Update NeoPixel animations (for error patterns)
    neopixelUpdate();

    // Service RTC 1 Hz update (if a tick occurred)
    rtcHandleUpdate();

    // ---- Logstart button handling ----
    static bool lastButton = false;
    bool nowButton = (digitalRead(PIN_LOGSTART_BUTTON) == HIGH);

    if (nowButton && !lastButton)
    {
        Serial.println("[BUTTON] Logstart pressed.");
        if (systemState == STATE_READY)
        {
            systemState = STATE_LOGGING;
            Serial.println("[STATE] LOGGING: logging started.");
            neopixelSetPattern(NEOPIXEL_PATTERN_LOGGING);

            // Start acquisition tasks ONCE, pinned to core 0.
            if (!samplingTasksStarted)
            {
                samplingTasksStarted = true;

                // Core split:
                //   Core 0: ADC + IMU sampling (these tasks)
                //   Core 1: this loop() + logging
                adcStartSamplingTask(0);
                imuStartSamplingTask(0);

                Serial.println("[TASK] ADC and IMU sampling tasks started on core 0.");
            }
        }
        else
        {
            Serial.printf("[STATE] Button press ignored (state=%d)\n", systemState);
        }
    }
    lastButton = nowButton;

    // ---- State-specific work ----
    switch (systemState)
    {
    case STATE_INIT:
        // Error/initialisation state; nothing more to do here
        break;

    case STATE_READY:
        // System ready; waiting for logstart button
        break;

    case STATE_LOGGING:
        // Logging loop (ADC/SD/IMU hooks will go here)
        loggingTick();
        break;
    }

    // Small delay; acquisition is on the other core so we can be relaxed here.
    delay(10);
}
