/**
 * @file main.cpp
 * @brief ESP32-S3 Loadcell Data Logger - Main Entry Point
 * 
 * High-rate load cell + IMU data logging to SD card with:
 * - RTC-disciplined timestamps
 * - Button-controlled logging sessions
 * - NeoPixel status indication
 * - Admin WebUI for calibration management
 * 
 * System Architecture:
 * - Core 0: SD card writes, WebUI, WiFi
 * - Core 1: ADC ISR (64 ksps), IMU reads
 * 
 * Operational Modes:
 * - User:       Normal operation (default on boot)
 * - FieldAdmin: Calibration and configuration
 * - Factory:    End-of-line testing
 */

// Set to 0 to disable verbose debug output during normal operation
#define DEBUG_VERBOSE 0

#include <Arduino.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include "pin_config.h"

// Drivers
#include "drivers/status_led.h"
#include "drivers/max11270.h"
#include "drivers/rx8900ce.h"
#include "drivers/lsm6dsv.h"
#include "drivers/max17048.h"
#include "drivers/sd_manager.h"

// Logging
#include "logging/ring_buffer.h"
#include "logging/timestamp_sync.h"
#include "logging/logger_module.h"
#include "logging/bin_to_csv.h"

// Calibration
#include "calibration/calibration_storage.h"
#include "calibration/calibration_interp.h"

// App
#include "app/app_mode.h"
#include "app/state_machine.h"

// Network
#include "network/wifi_ap.h"
#ifndef DISABLE_WEBUI
#include "network/admin_webui.h"
#endif

// Button state for debouncing and long press detection
namespace {
    bool lastButtonState = false;
    uint32_t lastButtonChangeMs = 0;
    bool buttonPressed = false;
    bool webServerStarted = false;
    
    // Long press detection
    uint32_t buttonPressStartMs = 0;
    bool longPressTriggered = false;
    const uint32_t LONG_PRESS_MS = 1000;
    
    // LED toggle state
    bool ledSteady = false;
    
    // ADC Ring Buffer (32KB = 4096 samples @ 8 bytes each)
    ADCRingBuffer adcBuffer;
    
    // Hardware initialization status
    bool adcOk = false;
    bool rtcOk = false;
    bool imuOk = false;
    bool fuelGaugeOk = false;
    bool sdOk = false;
    
    // Status print interval
    uint32_t lastStatusMs = 0;
    const uint32_t STATUS_INTERVAL_MS = 10000;
}

/**
 * @brief Scan I2C bus and report found devices
 */
void scanI2C() {
    // Initialize Wire (always needed)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
    
#if DEBUG_VERBOSE
    Serial.println("[I2C] Scanning bus...");
    
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] Found device at 0x%02X", addr);
            if (addr == I2C_ADDR_RX8900CE) Serial.print(" (RX8900CE RTC)");
            else if (addr == I2C_ADDR_LSM6DSV) Serial.print(" (LSM6DSV IMU)");
            else if (addr == I2C_ADDR_LSM6DSV_ALT) Serial.print(" (LSM6DSV IMU alt)");
            else if (addr == MAX17048::I2C_ADDRESS) Serial.print(" (MAX17048 Fuel Gauge)");
            Serial.println();
            found++;
        }
    }
    
    if (found == 0) {
        Serial.println("[I2C] No devices found! Check wiring and pull-ups.");
    } else {
        Serial.printf("[I2C] Found %d device(s)\n", found);
    }
    
    // Read WHO_AM_I from IMU address to identify actual chip variant
    Wire.beginTransmission(0x6A);  // LSM6DSx address
    Wire.write(0x0F);              // WHO_AM_I register
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x6A, (uint8_t)1);
    if (Wire.available()) {
        uint8_t whoami = Wire.read();
        Serial.printf("[I2C] Device 0x6A WHO_AM_I = 0x%02X\n", whoami);
        
        // Decode chip variant
        switch (whoami) {
            case 0x70: Serial.println("  -> LSM6DSV / LSM6DSV16X"); break;
            case 0x6C: Serial.println("  -> LSM6DSO / LSM6DSO32"); break;
            case 0x6A: Serial.println("  -> LSM6DS3"); break;
            case 0x69: Serial.println("  -> LSM6DS3TR-C"); break;
            case 0x6B: Serial.println("  -> LSM6DSL"); break;
            case 0x6D: Serial.println("  -> LSM6DSR"); break;
            default:   Serial.println("  -> UNKNOWN variant!"); break;
        }
    } else {
        Serial.println("[I2C] Failed to read WHO_AM_I from 0x6A");
    }
#endif
}

/**
 * @brief Initialize all hardware drivers
 */
bool initHardware() {
#if DEBUG_VERBOSE
    Serial.println("[Init] Starting hardware initialization...");
#endif
    
    // Status LED (first for visual feedback)
    StatusLED::init();
    StatusLED::setState(StatusLED::State::Init);
    
    // I2C bus scan (for debugging)
    scanI2C();
    
    // RTC (needed for timestamps) - uses Arduino Wire
    if (RX8900CE::init()) {
        rtcOk = true;
        
        // Check if time needs to be synced
        if (RX8900CE::needsTimeSync()) {
            // Sync to compile time as fallback
            RX8900CE::syncToCompileTime();
        }
#if DEBUG_VERBOSE
        else {
            // Show current time
            time_t epoch = RX8900CE::getEpoch();
            char timeBuf[24];
            RX8900CE::formatTime(epoch, timeBuf, sizeof(timeBuf));
            Serial.printf("[Init] RTC OK (%s)\n", timeBuf);
        }
#endif
        // Enable 1Hz output for timestamp discipline
        RX8900CE::enableFOUT1Hz();
    }
    
    // Timestamp sync
    if (rtcOk) {
        TimestampSync::init();
    }
    
    // Fuel Gauge (shares I2C bus)
    if (MAX17048::init()) {
        fuelGaugeOk = true;
    }
    
    // IMU (shares I2C bus with RTC via Arduino Wire)
    if (LSM6DSV::init()) {
        imuOk = true;
        
        // Configure at lower ODR for reliability
        LSM6DSV::configure(LSM6DSV::ODR::Hz120,
                           LSM6DSV::AccelScale::G2, 
                           LSM6DSV::GyroScale::DPS250);
        
#if DEBUG_VERBOSE
        // Diagnostic: Read back control registers to verify writes
        Serial.println("[IMU] Register readback diagnostic:");
        uint8_t ctrl1, ctrl2, ctrl3;
        LSM6DSV::readRegister(LSM6DSV::Reg::CTRL1, &ctrl1);
        LSM6DSV::readRegister(LSM6DSV::Reg::CTRL2, &ctrl2);
        LSM6DSV::readRegister(LSM6DSV::Reg::CTRL3, &ctrl3);
        Serial.printf("  CTRL1 (accel): 0x%02X - ODR bits[7:4]=0x%X, FS bits[3:2]=0x%X\n", 
                      ctrl1, (ctrl1 >> 4) & 0x0F, (ctrl1 >> 2) & 0x03);
        Serial.printf("  CTRL2 (gyro):  0x%02X - ODR bits[7:4]=0x%X, FS bits[3:0]=0x%X\n", 
                      ctrl2, (ctrl2 >> 4) & 0x0F, ctrl2 & 0x0F);
        Serial.printf("  CTRL3:         0x%02X - BDU=%d, IF_INC=%d\n", 
                      ctrl3, (ctrl3 >> 6) & 0x01, (ctrl3 >> 2) & 0x01);
        
        // Check expected values for ODR=120Hz (should be 0x04 in bits[7:4])
        if ((ctrl1 >> 4) == 0) {
            Serial.println("  ERROR: Accel ODR=0 (power down)!");
        }
        
        // Deep accelerometer diagnostic
        Serial.println("[IMU] Deep accelerometer diagnostic:");
        
        // 1. Check STATUS_REG for data ready flags
        uint8_t status;
        LSM6DSV::readRegister(LSM6DSV::Reg::STATUS_REG, &status);
        Serial.printf("  STATUS_REG: 0x%02X - XLDA=%d, GDA=%d, TDA=%d\n",
                      status, status & 0x01, (status >> 1) & 0x01, (status >> 2) & 0x01);
        
        // 2. Check additional config registers
        uint8_t ctrl5, ctrl6, ctrl8;
        Wire.beginTransmission(0x6A);
        Wire.write(0x14);  // CTRL5
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)1);
        ctrl5 = Wire.read();
        
        Wire.beginTransmission(0x6A);
        Wire.write(0x15);  // CTRL6
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)1);
        ctrl6 = Wire.read();
        
        Wire.beginTransmission(0x6A);
        Wire.write(0x17);  // CTRL8
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)1);
        ctrl8 = Wire.read();
        
        Serial.printf("  CTRL5: 0x%02X, CTRL6: 0x%02X, CTRL8: 0x%02X\n", ctrl5, ctrl6, ctrl8);
        
        // 3. Read raw accel output registers directly via Wire
        Wire.beginTransmission(0x6A);
        Wire.write(0x28);  // OUTX_L_A
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)6);
        int16_t rawX = Wire.read() | (Wire.read() << 8);
        int16_t rawY = Wire.read() | (Wire.read() << 8);
        int16_t rawZ = Wire.read() | (Wire.read() << 8);
        Serial.printf("  Direct Wire read OUTX/Y/Z_A (0x28): %d, %d, %d\n", rawX, rawY, rawZ);
        
        // 4. Also try reading gyro via Wire for comparison
        Wire.beginTransmission(0x6A);
        Wire.write(0x22);  // OUTX_L_G
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)6);
        int16_t gX = Wire.read() | (Wire.read() << 8);
        int16_t gY = Wire.read() | (Wire.read() << 8);
        int16_t gZ = Wire.read() | (Wire.read() << 8);
        Serial.printf("  Direct Wire read OUTX/Y/Z_G (0x22): %d, %d, %d\n", gX, gY, gZ);
        
        // 5. Full control register dump (0x10-0x1A)
        Serial.println("[IMU] Full control register dump:");
        for (uint8_t reg = 0x10; reg <= 0x1A; reg++) {
            uint8_t val;
            Wire.beginTransmission(0x6A);
            Wire.write(reg);
            Wire.endTransmission(false);
            Wire.requestFrom((uint8_t)0x6A, (uint8_t)1);
            val = Wire.read();
            Serial.printf("  REG 0x%02X = 0x%02X\n", reg, val);
        }
        
        // 6. Check FUNC_CFG_ACCESS (0x01)
        uint8_t funcCfg;
        Wire.beginTransmission(0x6A);
        Wire.write(0x01);  // FUNC_CFG_ACCESS
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)1);
        funcCfg = Wire.read();
        Serial.printf("[IMU] FUNC_CFG_ACCESS (0x01): 0x%02X\n", funcCfg);
        
        // 7. Ensure main register bank is selected (clear FUNC_CFG_ACCESS)
        Wire.beginTransmission(0x6A);
        Wire.write(0x01);  // FUNC_CFG_ACCESS
        Wire.write(0x00);  // Ensure main bank selected
        Wire.endTransmission();
        
        // 8. Try setting CTRL9 DEVICE_CONF bit (bit 1)
        Serial.println("[IMU] Attempting CTRL9 DEVICE_CONF fix:");
        uint8_t ctrl9_before;
        Wire.beginTransmission(0x6A);
        Wire.write(0x18);  // CTRL9
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)1);
        ctrl9_before = Wire.read();
        Serial.printf("  CTRL9 before: 0x%02X\n", ctrl9_before);
        
        // Set DEVICE_CONF bit (bit 1)
        uint8_t ctrl9_new = ctrl9_before | 0x02;
        Wire.beginTransmission(0x6A);
        Wire.write(0x18);
        Wire.write(ctrl9_new);
        Wire.endTransmission();
        Serial.printf("  CTRL9 after setting DEVICE_CONF: 0x%02X\n", ctrl9_new);
        
        // Wait for configuration to take effect
        delay(20);
        
        // Re-read accelerometer after DEVICE_CONF fix
        Wire.beginTransmission(0x6A);
        Wire.write(0x28);  // OUTX_L_A
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x6A, (uint8_t)6);
        int16_t rawX2 = Wire.read() | (Wire.read() << 8);
        int16_t rawY2 = Wire.read() | (Wire.read() << 8);
        int16_t rawZ2 = Wire.read() | (Wire.read() << 8);
        Serial.printf("  Accel after DEVICE_CONF: %d, %d, %d\n", rawX2, rawY2, rawZ2);
        
        if (rawX2 != 0 || rawY2 != 0 || rawZ2 != 0) {
            Serial.println("  SUCCESS! DEVICE_CONF fixed the accelerometer!");
        } else {
            Serial.println("  Still zeros - DEVICE_CONF did not help.");
        }
        
        // Wait longer for first data to be ready
        delay(50);
        
        // Test read with raw values
        LSM6DSV::RawData testData;
        if (LSM6DSV::readRaw(&testData)) {
            Serial.printf("[IMU] Raw: accel(%d,%d,%d) gyro(%d,%d,%d)\n", 
                testData.accel[0], testData.accel[1], testData.accel[2],
                testData.gyro[0], testData.gyro[1], testData.gyro[2]);
            
            // Check if accel values are all zero (suspicious)
            if (testData.accel[0] == 0 && testData.accel[1] == 0 && testData.accel[2] == 0) {
                Serial.println("[IMU] WARNING: Accel reads all zeros!");
            }
            Serial.println("OK");
        } else {
            Serial.println("[IMU] Read failed!");
        }
#endif  // DEBUG_VERBOSE (IMU diagnostics)
    }
    
    // SD Card
    Serial.print("[SD] ");
    if (SDManager::init()) {
        if (SDManager::mount()) {
            sdOk = true;
            SDManager::CardInfo info;
            if (SDManager::getCardInfo(&info)) {
                Serial.printf("OK (%s, %llu MB)\n", 
                    SDManager::getCardTypeString(),
                    info.totalBytes / (1024 * 1024));
            } else {
                Serial.println("OK");
            }
        } else {
            Serial.println("No card");
        }
    } else {
        Serial.println("FAILED");
    }
    
    // ADC (last - highest priority)
    Serial.print("[Init] ADC MAX11270... ");
    if (MAX11270::init()) {
        adcOk = true;
        MAX11270::Config adcConfig;
        adcConfig.rate = MAX11270::Rate::SPS_64000;
        adcConfig.gain = MAX11270::Gain::X128;
        MAX11270::configure(adcConfig);
        
        int32_t test = MAX11270::readSingle(100);
        if (test != INT32_MIN) {
            Serial.printf("OK (test: %ld)\n", test);
        } else {
            Serial.println("OK (no signal)");
        }
    } else {
        Serial.println("FAILED");
    }
    
    return rtcOk && imuOk && sdOk && adcOk;
}

/**
 * @brief Initialize software modules
 */
bool initSoftware() {
#if DEBUG_VERBOSE
    Serial.println("[Init] Starting software initialization...");
#endif
    
    // App mode
    AppMode::init();
    
    // Calibration storage
    if (CalibrationStorage::init()) {
        // Initialize interpolation
        CalibrationInterp::init();
    }
    
    // State machine
    StateMachine::init();
    
    // Logger module
    Logger::Config logConfig = Logger::defaultConfig();
    Logger::init(logConfig);
    
#ifndef DISABLE_WEBUI
    // WebUI
    AdminWebUI::init();
#endif
    
    return true;
}

/**
 * @brief Handle short button press
 */
void handleShortPress() {
#if DEBUG_VERBOSE
    Serial.println("[Button] Short press");
#endif
    if (AppMode::getMode() == AppMode::Mode::Factory) {
        StatusLED::nextTestState();
        Serial.println("[LED] Test: " + String(StatusLED::getTestStateName()));
    } else {
        // Process through state machine
        StateMachine::handleButtonPress(false);
    }
}

/**
 * @brief Handle long button press
 */
void handleLongPress() {
#if DEBUG_VERBOSE
    Serial.println("[Button] Long press");
#endif
    
    // Cycle mode: User -> FieldAdmin -> Factory -> User
    AppMode::Mode current = AppMode::getMode();
    AppMode::Mode next;
    
    switch (current) {
        case AppMode::Mode::User:
            next = AppMode::Mode::FieldAdmin;
            break;
        case AppMode::Mode::FieldAdmin:
            next = AppMode::Mode::Factory;
            break;
        default:
            next = AppMode::Mode::User;
            break;
    }
    
    AppMode::forceMode(next);
    ledSteady = false;
    
    // Update LED
    switch (next) {
        case AppMode::Mode::User:
            StatusLED::setState(StatusLED::State::IdleUser);
            Serial.println("[Mode] User");
            break;
        case AppMode::Mode::FieldAdmin:
            StatusLED::setState(StatusLED::State::IdleAdmin);
            Serial.println("[Mode] FieldAdmin");
            break;
        case AppMode::Mode::Factory:
            StatusLED::setState(StatusLED::State::IdleFactory);
            Serial.println("[Mode] Factory");
            break;
    }
}

/**
 * @brief Handle button input with debouncing
 */
void handleButton() {
    bool currentState = (digitalRead(PIN_LOG_BUTTON) == BUTTON_ACTIVE_LEVEL);
    uint32_t now = millis();
    
    if (currentState != lastButtonState) {
        lastButtonChangeMs = now;
        lastButtonState = currentState;
    }
    
    if ((now - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS) {
        if (currentState && !buttonPressed) {
            buttonPressed = true;
            buttonPressStartMs = now;
            longPressTriggered = false;
        }
        
        if (currentState && buttonPressed && !longPressTriggered) {
            if ((now - buttonPressStartMs) >= LONG_PRESS_MS) {
                longPressTriggered = true;
                handleLongPress();
            }
        }
        
        if (!currentState && buttonPressed) {
            if (!longPressTriggered) {
                handleShortPress();
            }
            buttonPressed = false;
        }
    }
}

/**
 * @brief Print periodic status
 */
void printStatus() {
#if !DEBUG_VERBOSE
    return;  // Disable periodic status during debugging
#endif
    uint32_t now = millis();
    if (now - lastStatusMs < STATUS_INTERVAL_MS) return;
    lastStatusMs = now;
    
    Serial.println();
    Serial.println("--- Status ---");
    Serial.printf("State: %s\n", StateMachine::getStateName());
    Serial.printf("Mode: %s\n", AppMode::getModeString());
    
    // Time and sync status
    if (rtcOk) {
        time_t epoch = TimestampSync::isSynchronized() 
                       ? TimestampSync::getEpochSeconds() 
                       : RX8900CE::getEpoch();
        char timeBuf[24];
        RX8900CE::formatTime(epoch, timeBuf, sizeof(timeBuf));
        Serial.printf("Time: %s (epoch: %lu)\n", timeBuf, (uint32_t)epoch);
        
        // Sync stats
        TimestampSync::SyncStatus syncStatus = TimestampSync::getStatus();
        float temp = TimestampSync::getRTCTemperature();
        
        Serial.printf("Sync: %lu pulses, drift: %ld ppm, temp: %.1f°C\n",
            syncStatus.pulseCount, syncStatus.driftPPM, temp);
        
        if (!syncStatus.synchronized) {
            Serial.println("  [!] Not synchronized - waiting for RTC pulses");
        } else if (syncStatus.lastPulseAgeMs > 2000) {
            Serial.printf("  [!] Last pulse %lu ms ago\n", syncStatus.lastPulseAgeMs);
        }
    }
    
    // Logging status
    if (Logger::isRunning()) {
        Logger::Status logStatus = Logger::getStatus();
        Serial.printf("Logging: %llu ADC, %llu IMU, %lu bytes\n",
            logStatus.samplesLogged, logStatus.imuSamplesLogged,
            logStatus.bytesWritten);
        if (logStatus.droppedSamples > 0) {
            Serial.printf("  [!] DROPPED: %lu samples!\n", logStatus.droppedSamples);
        }
    }
    
    // SD card status
    if (SDManager::isMounted()) {
        uint64_t free = SDManager::getFreeBytes();
        Serial.printf("SD: %llu MB free\n", free / (1024 * 1024));
    } else if (sdOk) {
        Serial.println("SD: Not mounted");
    }
    
    // IMU status (Z-axis should show ~1g when board is flat with Z up)
    if (imuOk) {
        LSM6DSV::RawData raw;
        if (LSM6DSV::readRaw(&raw)) {
            // Print raw values to diagnose if issue is read vs conversion
            Serial.printf("IMU raw: accel(%d, %d, %d) gyro(%d, %d, %d)\n",
                raw.accel[0], raw.accel[1], raw.accel[2],
                raw.gyro[0], raw.gyro[1], raw.gyro[2]);
            
            // Also print scaled values
            LSM6DSV::ScaledData scaled;
            if (LSM6DSV::readScaled(&scaled)) {
                Serial.printf("IMU scaled: accel(%.2f, %.2f, %.2f)g  gyro(%.1f, %.1f, %.1f)dps\n",
                    scaled.accel[0], scaled.accel[1], scaled.accel[2],
                    scaled.gyro[0], scaled.gyro[1], scaled.gyro[2]);
            }
        } else {
            Serial.println("IMU: read failed");
        }
    }
    
    // Battery status
    if (fuelGaugeOk) {
        MAX17048::BatteryData batt;
        if (MAX17048::getBatteryData(&batt)) {
            Serial.printf("Battery: %.2fV, %.1f%%, rate: %.1f%%/hr\n",
                batt.voltage, batt.socPercent, batt.chargeRate);
        }
    }
    
    Serial.println("--------------");
    Serial.println();
}

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);
    
    Serial.println();
    Serial.println("=== Loadcell Logger v1.0 ===");
    
    // Button
    pinMode(PIN_LOG_BUTTON, INPUT);
    
    // Configure task watchdog (5 second timeout, panic on timeout)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    
    // Hardware
    bool hwOk = initHardware();
    
    // Software
    initSoftware();
    
    // WiFi
    WiFiAP::start();
    Serial.printf("[WiFi] http://%s\n", WiFiAP::getIP().c_str());
    
    // Hardware summary
    Serial.printf("[Init] ADC:%s RTC:%s IMU:%s SD:%s\n", 
        adcOk ? "OK" : "FAIL", rtcOk ? "OK" : "FAIL", 
        imuOk ? "OK" : "FAIL", sdOk ? "OK" : "FAIL");
    
    if (adcOk) {
        // ADC is critical - proceed even if other parts fail
        StatusLED::setState(StatusLED::State::IdleUser);
        StateMachine::processEvent(StateMachine::Event::InitComplete);
    } else {
        // ADC failure is critical
        Serial.println("[ERROR] ADC init failed!");
        StateMachine::setError(StateMachine::ErrorCode::AdcError);
    }
    
    Serial.println();
    Serial.println("[Init] Complete");
    Serial.printf("[Init] Mode: %s, State: %s\n", 
        AppMode::getModeString(), StateMachine::getStateName());
    Serial.println();
}

void loop() {
#ifndef DISABLE_WEBUI
    // Start web server when WiFi ready
    if (!webServerStarted && WiFiAP::isReady()) {
        if (AdminWebUI::beginServer()) {
            Serial.println("[WebUI] Server started");
            webServerStarted = true;
        }
    }
#endif
    
    // Core updates
        StatusLED::update();
    TimestampSync::update();
    StateMachine::update();
    
    // Button
    handleButton();
    
    // Status (debug)
    printStatus();
    
    // Small delay to prevent watchdog issues
    delay(1);
}
