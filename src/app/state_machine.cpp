/**
 * @file state_machine.cpp
 * @brief Implementation of main system state machine
 */

#include "state_machine.h"
#include "../drivers/max11270_driver.h"
#include "../drivers/lsm6dsv_driver.h"
#include "../drivers/rx8900ce_driver.h"
#include "../drivers/sd_manager.h"
#include "../network/wifi_ap.h"
#include "../network/admin_webui.h"
#include "../calibration/calibration_storage.h"
#include "../calibration/calibration_interp.h"
#include "../logging/timestamp_sync.h"
#include "../logging/logger_module.h"
#include "../logging/bin_to_csv.h"
#include <Wire.h>

bool StateMachine::begin() {
    Serial.println("=== State Machine Initialization ===");
    
    // Allocate modules
    adc = new MAX11270Driver();
    imu = new LSM6DSVDriver();
    rtc = new RX8900CEDriver();
    sd = new SDManager();
    led = new StatusLED();
    wifi = new WiFiAP();
    webui = new AdminWebUI();
    cal_storage = new CalibrationStorage();
    cal_interp = new CalibrationInterp();
    timestamp_sync = new TimestampSync();
    logger = new LoggerModule();
    converter = new BinToCSVConverter();
    
    // Initialize button
    pinMode(PIN_LOG_BUTTON, INPUT);
    button_pressed = false;
    button_was_pressed = false;
    last_button_time = 0;
    
    // Initialize status LED
    led->begin();
    led->setState(STATE_INIT);
    
    // Initialize hardware
    if (!initHardware()) {
        Serial.println("ERROR: Hardware initialization failed");
        led->setState(STATE_ERROR);
        return false;
    }
    
    // Initialize application modules
    if (!initModules()) {
        Serial.println("ERROR: Module initialization failed");
        led->setState(STATE_ERROR);
        return false;
    }
    
    // Start in ADMIN state
    changeState(STATE_ADMIN);
    
    Serial.println("=== Initialization Complete ===");
    
    return true;
}

bool StateMachine::initHardware() {
    Serial.println("Initializing hardware...");
    
    // Initialize I2C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
    Serial.println("I2C: Initialized");
    
    // Initialize RTC
    if (!rtc->begin(&Wire)) {
        Serial.println("RTC: FAILED");
        return false;
    }
    Serial.println("RTC: OK");
    
    // Check RTC is running
    if (!rtc->isRunning()) {
        Serial.println("WARNING: RTC not running, setting default time");
        DateTime dt;
        dt.year = 2025;
        dt.month = 1;
        dt.day = 1;
        dt.hour = 0;
        dt.minute = 0;
        dt.second = 0;
        rtc->setDateTime(dt);
    }
    
    // Initialize IMU
    if (!imu->begin(&Wire)) {
        Serial.println("IMU: FAILED");
        return false;
    }
    Serial.println("IMU: OK");
    
    // Initialize ADC
    if (!adc->begin()) {
        Serial.println("ADC: FAILED");
        return false;
    }
    Serial.println("ADC: OK");
    
    // Perform ADC self-calibration
    Serial.println("ADC: Self-calibration...");
    if (adc->performSelfCalibration()) {
        Serial.println("ADC: Calibration complete");
    } else {
        Serial.println("ADC: Calibration warning (continuing anyway)");
    }
    
    // Initialize SD card
    if (!sd->begin()) {
        Serial.println("SD: FAILED");
        return false;
    }
    
    if (!sd->mount()) {
        Serial.println("WARNING: SD card not present or mount failed");
    } else {
        Serial.println("SD: OK");
    }
    
    return true;
}

bool StateMachine::initModules() {
    Serial.println("Initializing modules...");
    
    // Initialize calibration storage
    if (!cal_storage->begin()) {
        Serial.println("Cal Storage: FAILED");
        return false;
    }
    Serial.printf("Cal Storage: OK (%u loadcells stored)\n", cal_storage->getLoadcellCount());
    
    // Load active calibration if available
    LoadcellCalibration active_cal;
    if (cal_storage->getActiveLoadcell(active_cal)) {
        cal_interp->setCalibration(active_cal);
        Serial.printf("Active loadcell: %s\n", active_cal.id);
    } else {
        Serial.println("No active loadcell set");
    }
    
    // Initialize timestamp sync
    uint32_t rtc_time = rtc->getUnixTime();
    if (!timestamp_sync->begin(rtc_time)) {
        Serial.println("Timestamp Sync: FAILED");
        return false;
    }
    Serial.println("Timestamp Sync: OK");
    
    // Initialize logger
    if (!logger->begin(adc, imu, sd, timestamp_sync)) {
        Serial.println("Logger: FAILED");
        return false;
    }
    Serial.println("Logger: OK");
    
    // Initialize converter
    if (!converter->begin(sd, cal_interp)) {
        Serial.println("Converter: FAILED");
        return false;
    }
    Serial.println("Converter: OK");
    
    // Initialize WiFi AP
    if (!wifi->start()) {
        Serial.println("WiFi: FAILED");
        return false;
    }
    Serial.println("WiFi: OK");
    
    // Initialize WebUI
    if (!webui->begin(cal_storage, cal_interp, adc)) {
        Serial.println("WebUI: FAILED");
        return false;
    }
    
    if (!webui->start()) {
        Serial.println("WebUI: Start FAILED");
        return false;
    }
    Serial.println("WebUI: OK");
    
    return true;
}

bool StateMachine::readButton() {
    uint32_t now = millis();
    bool current = (digitalRead(PIN_LOG_BUTTON) == BUTTON_ACTIVE_LEVEL);
    
    // Debounce
    if (now - last_button_time < BUTTON_DEBOUNCE_MS) {
        return button_pressed;
    }
    
    if (current != button_pressed) {
        last_button_time = now;
        button_pressed = current;
    }
    
    return button_pressed;
}

void StateMachine::handleButtonPress() {
    bool button = readButton();
    
    // Detect rising edge
    if (button && !button_was_pressed) {
        Serial.println("Button: Pressed");
        
        switch (current_state) {
            case STATE_ADMIN:
                changeState(STATE_PRELOG);
                break;
                
            case STATE_LOGGING:
                changeState(STATE_STOPPING);
                break;
                
            case STATE_READY:
                changeState(STATE_ADMIN);
                break;
                
            default:
                break;
        }
    }
    
    button_was_pressed = button;
}

void StateMachine::changeState(SystemState new_state) {
    Serial.printf("State: %d -> %d\n", current_state, new_state);
    
    current_state = new_state;
    state_enter_time = millis();
    led->setState(new_state);
}

void StateMachine::update() {
    // Update LED pattern
    led->update();
    
    // Handle button input
    handleButtonPress();
    
    // State-specific updates
    switch (current_state) {
        case STATE_INIT:
            updateInit();
            break;
        case STATE_ADMIN:
            updateAdmin();
            break;
        case STATE_PRELOG:
            updatePreLog();
            break;
        case STATE_LOGGING:
            updateLogging();
            break;
        case STATE_STOPPING:
            updateStopping();
            break;
        case STATE_CONVERTING:
            updateConverting();
            break;
        case STATE_READY:
            updateReady();
            break;
        case STATE_ERROR:
            // Stay in error state
            break;
    }
}

void StateMachine::updateInit() {
    // Transition handled in begin()
}

void StateMachine::updateAdmin() {
    // WiFi and WebUI are active
    // Wait for button press to start logging
}

void StateMachine::updatePreLog() {
    // Shutting down WiFi
    Serial.println("PreLog: Shutting down WiFi...");
    
    webui->stop();
    wifi->stop();
    
    delay(500);  // Give WiFi time to shutdown
    
    // Check if we have an active loadcell
    LoadcellCalibration active_cal;
    if (!cal_storage->getActiveLoadcell(active_cal)) {
        Serial.println("ERROR: No active loadcell selected");
        changeState(STATE_ERROR);
        return;
    }
    
    // Reload calibration
    cal_interp->setCalibration(active_cal);
    
    // Transition to logging
    changeState(STATE_LOGGING);
}

void StateMachine::updateLogging() {
    // On first entry, start logging
    if (millis() - state_enter_time < 100) {
        LoadcellCalibration active_cal;
        if (cal_storage->getActiveLoadcell(active_cal)) {
            if (logger->startLogging(active_cal)) {
                last_log_file = logger->getCurrentLogFile();
                Serial.println("Logging: Started successfully");
            } else {
                Serial.println("ERROR: Failed to start logging");
                changeState(STATE_ERROR);
                return;
            }
        }
    }
    
    // Print stats periodically
    static uint32_t last_stats = 0;
    if (millis() - last_stats > 5000) {
        last_stats = millis();
        auto stats = logger->getStats();
        Serial.printf("Logging: %u samples, %u IMU, %.1f%% fill\n",
                     stats.samples_acquired, stats.imu_samples, stats.fill_percent);
    }
}

void StateMachine::updateStopping() {
    Serial.println("Stopping: Flushing buffers...");
    
    logger->stopLogging();
    
    Serial.println("Stopping: Complete");
    changeState(STATE_CONVERTING);
}

void StateMachine::updateConverting() {
    if (millis() - state_enter_time < 100) {
        Serial.println("Converting: Binary to CSV...");
        
        // Convert with progress callback
        bool success = converter->convert(last_log_file.c_str(), [](int progress) {
            static int last_p = -1;
            if (progress != last_p && progress % 10 == 0) {
                Serial.printf("Converting: %d%%\n", progress);
                last_p = progress;
            }
        });
        
        if (success) {
            Serial.println("Converting: Complete");
            Serial.printf("CSV file: %s\n", converter->getLastCSVPath().c_str());
            changeState(STATE_READY);
        } else {
            Serial.println("ERROR: Conversion failed");
            changeState(STATE_ERROR);
        }
    }
}

void StateMachine::updateReady() {
    // SD card can be safely removed
    // Wait for button press or timeout to restart WiFi
    
    if (millis() - state_enter_time > 30000) {
        // Auto-transition back to admin after 30 seconds
        Serial.println("Ready: Timeout, restarting WiFi...");
        
        wifi->start();
        webui->start();
        
        changeState(STATE_ADMIN);
    }
}
