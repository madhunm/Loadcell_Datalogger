#include <Arduino.h>
#include <Wire.h>
#include <cstring>  // For memset
#include "esp_task_wdt.h"

#include "pins.h"
#include "gpio.h"
#include "sdcard.h"
#include "rtc.h"
#include "imu.h"
#include "neopixel.h"
#include "adc.h"
#include "logger.h"
#include "webconfig.h"

// ---- System state machine ----
enum SystemState
{
    STATE_INIT = 0,
    STATE_READY,
    STATE_LOGGING,
    STATE_CONVERTING  // Converting binary logs to CSV
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

// ---- Logger configuration ----
// This will be passed to loggerStartSession()
static LoggerConfig makeLoggerConfig()
{
    LoggerConfig config;
    config.adcSampleRate = 64000;  // 64 ksps
    config.adcPgaGain    = ADC_PGA_GAIN_4;  // Matches initPeripherals()
    config.imuAccelRange = 16;      // ±16 g
    config.imuGyroRange  = 2000;    // 2000 dps
    config.imuOdr        = 960;     // 960 Hz
    return config;
}

void setup()
{
    delay(500);

    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("Loadcell Logger – ESP32-S3 bring-up");
    Serial.println("------------------------------------");

    // Initialize watchdog timer (5 second timeout)
    // This prevents system hangs and detects infinite loops
    // Note: ESP32 Arduino framework uses a config struct
    esp_task_wdt_config_t wdt_config;
    memset(&wdt_config, 0, sizeof(wdt_config));
    wdt_config.timeout_ms = 5000;  // 5 second timeout
    wdt_config.idle_core_mask = 0; // Don't monitor idle tasks
    wdt_config.trigger_panic = true; // Panic on timeout
    esp_err_t err = esp_task_wdt_init(&wdt_config);
    if (err != ESP_OK)
    {
        Serial.printf("[INIT] WARNING: Watchdog init failed: %d\n", err);
    }
    else
    {
        esp_task_wdt_add(NULL);      // Add main loop task to watchdog
        Serial.println("[INIT] Watchdog timer initialized (5s timeout)");
    }

    // Initialize GPIO pins
    gpioInit();
    Serial.printf("GPIO initialized. I2C started on SDA=%d SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);

    // NeoPixel
    neopixelInit();
    neopixelSetPattern(NEOPIXEL_PATTERN_INIT);

    // Logger initialisation
    loggerInit();

    // WiFi Access Point and Web Server initialization
    if (webConfigInit())
    {
        Serial.println("[WEBCONFIG] Web configuration interface started");
    }
    else
    {
        Serial.println("[WEBCONFIG] Warning: Failed to start web configuration interface");
    }

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
    // Reset watchdog timer - must be called regularly to prevent reset
    // This ensures the main loop is running and not stuck
    esp_task_wdt_reset();

    // Handle web server requests
    webConfigHandleClient();

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
            // Start acquisition tasks ONCE, pinned to core 0.
            if (!samplingTasksStarted)
            {
                samplingTasksStarted = true;

                // Core split:
                //   Core 0: ADC + IMU sampling (these tasks)
                //   Core 1: this loop() + logging
                
                // Start ADC task and verify it was created
                if (!adcStartSamplingTask(0))
                {
                    Serial.println("[ERROR] Failed to create ADC sampling task!");
                    neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_ADC);
                    samplingTasksStarted = false; // Allow retry
                    return; // Don't start logging if tasks failed
                }
                
                // Start IMU task and verify it was created
                if (!imuStartSamplingTask(0))
                {
                    Serial.println("[ERROR] Failed to create IMU sampling task!");
                    neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_IMU);
                    samplingTasksStarted = false; // Allow retry
                    return; // Don't start logging if tasks failed
                }
                
                Serial.println("[TASK] ADC and IMU sampling tasks started on core 0.");
            }

            // Start logging session (use web config if available, otherwise default)
            LoggerConfig config = webConfigIsActive() ? webConfigGetLoggerConfig() : makeLoggerConfig();
            if (loggerStartSession(config))
            {
                systemState = STATE_LOGGING;
                Serial.println("[STATE] LOGGING: logging started.");
                neopixelSetPattern(NEOPIXEL_PATTERN_LOGGING);
            }
            else
            {
                Serial.println("[ERROR] Failed to start logging session.");
                neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
            }
        }
        else if (systemState == STATE_LOGGING)
        {
            // Stop logging session and start conversion
            if (loggerStopSessionAndFlush())
            {
                systemState = STATE_CONVERTING;
                Serial.println("[STATE] CONVERTING: logging stopped, starting CSV conversion...");
                neopixelSetPattern(NEOPIXEL_PATTERN_CONVERTING);
            }
            else
            {
                Serial.println("[ERROR] Failed to stop logging session.");
            }
        }
        else if (systemState == STATE_CONVERTING)
        {
            // Conversion in progress - ignore button press
            Serial.println("[BUTTON] Conversion in progress, button press ignored.");
        }
        // Note: STATE_READY button press is already handled above (starts logging)
        // If we're in STATE_READY with SAFE_TO_REMOVE pattern, pressing button will start a new logging session
        // To reset the pattern without starting logging, user would need to wait or we could add a timeout
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
        // Logging loop: drain buffers and write to SD card
        loggerTick();
        break;

    case STATE_CONVERTING:
        // Convert binary logs to CSV (non-blocking task)
        // Check if conversion is complete
        bool conversionResult;
        if (loggerIsCsvConversionComplete(&conversionResult))
        {
            systemState = STATE_READY;
            if (conversionResult)
            {
                Serial.println("[STATE] READY: CSV conversion complete. Safe to remove SD card.");
                neopixelSetPattern(NEOPIXEL_PATTERN_SAFE_TO_REMOVE);
            }
            else
            {
                Serial.println("[ERROR] CSV conversion failed. Returning to ready state.");
                neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
            }
        }
        // If conversion still in progress, keep checking (neopixel updates in conversion task)
        break;
    }

    // Small delay; acquisition is on the other core so we can be relaxed here.
    delay(10);
}
