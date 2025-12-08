/**
 * @file main.cpp
 * @brief Main application entry point and system state machine
 * @details This file implements the main control loop, system state machine,
 *          peripheral initialization, and button handling for the Loadcell
 *          Datalogger. The system uses a dual-core architecture:
 *          - Core 0: High-priority sampling tasks (ADC, IMU)
 *          - Core 1: Main loop (logging, web server, state machine)
 * 
 * @author Loadcell Datalogger Project
 * @date December 2024
 */

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
#include "max17048.h"
#include "system.h"

// ============================================================================
// SYSTEM STATE MACHINE
// ============================================================================

#include "system.h"

/** @brief Current system state - initialized to INIT on boot */
SystemState systemState = STATE_INIT;

/** @brief Flag to ensure sampling tasks are started only once per session */
static bool samplingTasksStarted = false;

/** @brief External control flag for remote logging control */
volatile bool g_remoteLoggingRequest = false;
volatile bool g_remoteLoggingAction = false; // false = stop, true = start

// Get current system state (thread-safe)
SystemState systemGetState()
{
    return systemState;
}

// ============================================================================
// PERIPHERAL INITIALIZATION
// ============================================================================

/**
 * @brief Initialize all system peripherals
 * @details Initializes SD card, IMU, RTC, and ADC in sequence. On failure,
 *          sets appropriate NeoPixel error pattern and returns false.
 *          This function should be called once during system startup.
 * 
 * @return true if all peripherals initialized successfully, false otherwise
 */
// Retry helper function with exponential backoff (inline retry logic)
static bool retryInit(const char* name, bool result, NeopixelPattern errorPattern, uint32_t maxRetries = 3)
{
    if (result) return true;
    
    uint32_t delayMs = 100;  // Start with 100ms delay
    for (uint32_t attempt = 1; attempt < maxRetries; attempt++)
    {
        Serial.printf("[INIT] %s initialization failed, retrying in %u ms... (attempt %u/%u)\n", 
                     name, delayMs, attempt + 1, maxRetries);
        delay(delayMs);
        delayMs *= 2;  // Exponential backoff: 100ms, 200ms, 400ms
        // Note: Actual retry is done by caller
    }
    
    Serial.printf("[INIT] %s initialization failed after %u attempts\n", name, maxRetries);
    neopixelSetPattern(errorPattern);
    return false;
}

bool initPeripherals()
{
    // Try each peripheral in turn with retry logic; on failure, select a per-peripheral
    // error pattern and return false immediately.

    // SD Card with retry
    uint32_t delayMs = 100;
    bool sdOk = false;
    for (uint32_t attempt = 0; attempt < 3; attempt++)
    {
        if (sdCardInit())
        {
            sdOk = true;
            if (attempt > 0) Serial.printf("[INIT] SD card initialized after %u retries\n", attempt);
            break;
        }
        if (attempt < 2) { delay(delayMs); delayMs *= 2; }
    }
    if (!sdOk)
    {
        Serial.println("[INIT] SD card initialisation failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
        return false;
    }

    // IMU with retry
    delayMs = 100;
    bool imuOk = false;
    for (uint32_t attempt = 0; attempt < 3; attempt++)
    {
        if (imuInit(Wire))
        {
            imuOk = true;
            if (attempt > 0) Serial.printf("[INIT] IMU initialized after %u retries\n", attempt);
            break;
        }
        if (attempt < 2) { delay(delayMs); delayMs *= 2; }
    }
    if (!imuOk)
    {
        Serial.println("[INIT] IMU initialisation failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_IMU);
        return false;
    }

    // RTC with retry
    delayMs = 100;
    bool rtcOk = false;
    for (uint32_t attempt = 0; attempt < 3; attempt++)
    {
        if (rtcInit())
        {
            rtcOk = true;
            if (attempt > 0) Serial.printf("[INIT] RTC initialized after %u retries\n", attempt);
            break;
        }
        if (attempt < 2) { delay(delayMs); delayMs *= 2; }
    }
    if (!rtcOk)
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

    // ADC Init with retry
    delayMs = 100;
    bool adcOk = false;
    for (uint32_t attempt = 0; attempt < 3; attempt++)
    {
        if (adcInit(gain))
        {
            adcOk = true;
            if (attempt > 0) Serial.printf("[INIT] ADC initialized after %u retries\n", attempt);
            break;
        }
        if (attempt < 2) { delay(delayMs); delayMs *= 2; }
    }
    if (!adcOk)
    {
        Serial.println("[INIT][ADC] adcInit() (including self-cal) failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_ADC);
        return false;
    }

    // Start continuous conversion at 64 ksps (RATE code 0x0F) with retry
    delayMs = 100;
    bool adcStartOk = false;
    for (uint32_t attempt = 0; attempt < 3; attempt++)
    {
        if (adcStartContinuous(0x0F))
        {
            adcStartOk = true;
            if (attempt > 0) Serial.printf("[INIT] ADC continuous started after %u retries\n", attempt);
            break;
        }
        if (attempt < 2) { delay(delayMs); delayMs *= 2; }
    }
    if (!adcStartOk)
    {
        Serial.println("[INIT][ADC] adcStartContinuous() failed.");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_ADC);
        return false;
    }

    // Initialize MAX17048 fuel gauge (non-critical - don't fail if not present)
    if (max17048Init(Wire))
    {
        Max17048Status batteryStatus;
        if (max17048ReadStatus(&batteryStatus))
        {
            Serial.printf("[MAX17048] Battery: %.2fV, SOC: %.1f%%, Charge Rate: %.2f%%/hr\n",
                         batteryStatus.voltage, batteryStatus.soc, batteryStatus.chargeRate);
        }
    }
    else
    {
        Serial.println("[MAX17048] Fuel gauge not detected (optional component)");
    }

    return true;
}

// ============================================================================
// LOGGER CONFIGURATION
// ============================================================================

/**
 * @brief Create default logger configuration
 * @details Creates a LoggerConfig structure with default values. This is used
 *          if web configuration is not available. The configuration can be
 *          overridden via the web portal.
 * 
 * @return LoggerConfig structure with default values:
 *         - ADC: 64 ksps, PGA gain x4
 *         - IMU: 960 Hz, ±16g accel, ±2000 dps gyro
 */
static LoggerConfig makeLoggerConfig()
{
    LoggerConfig config;
    config.adcSampleRate = 64000;        // 64,000 samples per second
    config.adcPgaGain    = ADC_PGA_GAIN_4;  // x4 gain (matches initPeripherals())
    config.imuAccelRange = 16;           // ±16 g accelerometer range
    config.imuGyroRange  = 2000;         // ±2000 degrees per second
    config.imuOdr        = 960;          // 960 Hz output data rate
    return config;
}

// ============================================================================
// ARDUINO SETUP FUNCTION
// ============================================================================

/**
 * @brief Arduino setup function - called once on boot
 * @details Performs system initialization:
 *          1. Serial communication setup
 *          2. Watchdog timer initialization
 *          3. GPIO initialization
 *          4. NeoPixel initialization
 *          5. Logger initialization
 *          6. Web server initialization
 *          7. Peripheral initialization
 * 
 *          On successful initialization, system enters READY state.
 *          On failure, system remains in INIT state with error pattern.
 */
void setup()
{
    // Allow hardware to stabilize
    delay(500);

    // Initialize serial communication for debugging
    Serial.begin(115200);
    delay(500);  // Wait for serial port to be ready
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

// ============================================================================
// ARDUINO LOOP FUNCTION
// ============================================================================

/**
 * @brief Arduino main loop - called repeatedly
 * @details Main control loop that:
 *          1. Resets watchdog timer
 *          2. Handles web server requests
 *          3. Updates NeoPixel animations
 *          4. Services RTC interrupts
 *          5. Handles button presses
 *          6. Executes state-specific actions
 * 
 *          This loop runs on Core 1, while sampling tasks run on Core 0.
 */
void loop()
{
    // Reset watchdog timer - must be called regularly to prevent reset
    // This ensures the main loop is running and not stuck
    esp_task_wdt_reset();

    // Handle web server requests (non-blocking)
    webConfigHandleClient();

    // Update NeoPixel animations (for blinking/breathing patterns)
    neopixelUpdate();

    // Service RTC 1 Hz update interrupt (if a tick occurred)
    rtcHandleUpdate();

    // ---- Battery monitoring (check every 5 seconds) ----
    // Note: millis() wraps after ~49 days, but unsigned subtraction handles this correctly
    // (millis() - lastBatteryCheck) will work correctly even after wrap-around
    static uint32_t lastBatteryCheck = 0;
    static const uint32_t BATTERY_CHECK_INTERVAL_MS = 5000; // Check every 5 seconds
    static NeopixelPattern lastNonBatteryPattern = NEOPIXEL_PATTERN_OFF;
    
    if ((uint32_t)(millis() - lastBatteryCheck) >= BATTERY_CHECK_INTERVAL_MS)
    {
        lastBatteryCheck = millis();
        
        // Only check battery if MAX17048 is present
        if (max17048IsPresent())
        {
            Max17048Status batteryStatus;
            if (max17048ReadStatus(&batteryStatus))
            {
                // Low battery threshold: < 20% SOC
                const float LOW_BATTERY_THRESHOLD = 20.0f;
                
                // Check if battery is low
                if (batteryStatus.soc < LOW_BATTERY_THRESHOLD)
                {
                    // Only set low battery pattern if we're not in a critical error state
                    // Critical errors take priority over low battery warning
                    NeopixelPattern currentPattern = neopixelGetCurrentPattern();
                    if (currentPattern != NEOPIXEL_PATTERN_ERROR_SD &&
                        currentPattern != NEOPIXEL_PATTERN_ERROR_RTC &&
                        currentPattern != NEOPIXEL_PATTERN_ERROR_IMU &&
                        currentPattern != NEOPIXEL_PATTERN_ERROR_ADC &&
                        currentPattern != NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE &&
                        currentPattern != NEOPIXEL_PATTERN_ERROR_BUFFER_FULL)
                    {
                        // Save the current pattern if it's not already low battery
                        if (currentPattern != NEOPIXEL_PATTERN_LOW_BATTERY)
                        {
                            lastNonBatteryPattern = currentPattern;
                        }
                        neopixelSetPattern(NEOPIXEL_PATTERN_LOW_BATTERY);
                        Serial.printf("[BATTERY] Low battery warning: %.1f%% SOC, %.2fV\n", 
                                     batteryStatus.soc, batteryStatus.voltage);
                    }
                }
                else
                {
                    // Battery is OK - restore previous pattern if we were showing low battery
                    NeopixelPattern currentPattern = neopixelGetCurrentPattern();
                    if (currentPattern == NEOPIXEL_PATTERN_LOW_BATTERY)
                    {
                        neopixelSetPattern(lastNonBatteryPattern);
                        Serial.printf("[BATTERY] Battery OK: %.1f%% SOC, %.2fV\n", 
                                     batteryStatus.soc, batteryStatus.voltage);
                    }
                }
            }
        }
    }

    // ---- Remote logging control (from web interface) ----
    if (g_remoteLoggingRequest)
    {
        g_remoteLoggingRequest = false;
        bool shouldStart = g_remoteLoggingAction;
        
        if (shouldStart && systemState == STATE_READY)
        {
            // Start logging (same logic as button press)
            if (!samplingTasksStarted)
            {
                samplingTasksStarted = true;
                if (!adcStartSamplingTask(0))
                {
                    Serial.println("[ERROR] Failed to create ADC sampling task!");
                    neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_ADC);
                    samplingTasksStarted = false;
                    return;
                }
                if (!imuStartSamplingTask(0))
                {
                    Serial.println("[ERROR] Failed to create IMU sampling task!");
                    neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_IMU);
                    samplingTasksStarted = false;
                    return;
                }
                Serial.println("[TASK] ADC and IMU sampling tasks started on core 0.");
            }
            
            LoggerConfig config = webConfigIsActive() ? webConfigGetLoggerConfig() : makeLoggerConfig();
            if (loggerStartSession(config))
            {
                systemState = STATE_LOGGING;
                Serial.println("[STATE] LOGGING: logging started (remote).");
                neopixelSetPattern(NEOPIXEL_PATTERN_LOGGING);
            }
            else
            {
                Serial.println("[ERROR] Failed to start logging session (remote).");
                neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
            }
        }
        else if (!shouldStart && systemState == STATE_LOGGING)
        {
            // Stop logging (same logic as button press)
            if (loggerStopSessionAndFlush())
            {
                systemState = STATE_CONVERTING;
                Serial.println("[STATE] CONVERTING: logging stopped (remote), starting CSV conversion...");
                neopixelSetPattern(NEOPIXEL_PATTERN_CONVERTING);
                
                if (!loggerConvertLastSessionToCsv())
                {
                    Serial.println("[ERROR] Failed to start CSV conversion task.");
                    systemState = STATE_READY;
                    neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
                }
            }
            else
            {
                Serial.println("[ERROR] Failed to stop logging session (remote).");
            }
        }
    }

    // ---- Logstart button handling ----
    static bool lastButton = false;
    bool nowButton = (digitalRead(PIN_LOGSTART_BUTTON) == HIGH);

    if (nowButton && !lastButton)
    {
        Serial.println("[BUTTON] Logstart pressed.");
        if (systemState == STATE_READY)
        {
            // Start acquisition tasks ONCE, pinned to core 0.
            // Note: If ADC optimization was run, the sampling task may have been stopped.
            // This check ensures we restart it if needed.
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
                
                // Start CSV conversion task (non-blocking)
                if (!loggerConvertLastSessionToCsv())
                {
                    Serial.println("[ERROR] Failed to start CSV conversion task.");
                    systemState = STATE_READY;
                    neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
                }
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
