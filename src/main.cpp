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
 * Operational Modes:
 * - User:       Normal operation (default on boot)
 * - FieldAdmin: Calibration and configuration
 * - Factory:    End-of-line testing
 */

#include <Arduino.h>
#include "pin_config.h"
#include "drivers/status_led.h"
#include "app/app_mode.h"
#include "network/wifi_ap.h"
#include "network/admin_webui.h"

// Button state for debouncing and long press detection
namespace {
    bool lastButtonState = false;
    uint32_t lastButtonChangeMs = 0;
    bool buttonPressed = false;
    bool webServerStarted = false;  // Track if we've started the web server
    
    // Long press detection
    uint32_t buttonPressStartMs = 0;  // When button was pressed
    bool longPressTriggered = false;  // Prevent double-trigger
    const uint32_t LONG_PRESS_MS = 1000;  // 1 second for long press
    
    // LED toggle state for short press feedback
    bool ledSteady = false;
}

/**
 * @brief Handle short button press
 * 
 * In Factory mode: advances LED test state
 * In other modes: toggles LED between pulse and steady for visual feedback
 */
void handleShortPress() {
    Serial.println("[Main] Short press detected");
    
    if (AppMode::getMode() == AppMode::Mode::Factory) {
        // Factory mode: advance LED test
        StatusLED::nextTestState();
        Serial.println("[Main] LED test: " + String(StatusLED::getTestStateName()));
    } else {
        // Toggle LED between pulse and steady for visual feedback
        ledSteady = !ledSteady;
        
        if (ledSteady) {
            StatusLED::setState(StatusLED::State::Ready);  // Green steady
            Serial.println("[Main] LED: Steady green (simulating ready state)");
        } else {
            // Return to mode-appropriate idle state
            switch (AppMode::getMode()) {
                case AppMode::Mode::User:
                    StatusLED::setState(StatusLED::State::IdleUser);
                    Serial.println("[Main] LED: Blue pulse (User idle)");
                    break;
                case AppMode::Mode::FieldAdmin:
                    StatusLED::setState(StatusLED::State::IdleAdmin);
                    Serial.println("[Main] LED: Cyan pulse (Admin idle)");
                    break;
                default:
                    break;
            }
        }
    }
}

/**
 * @brief Handle long button press (≥1 second)
 * 
 * Cycles through modes: User → FieldAdmin → Factory → User
 * Bypasses password check for hardware button access.
 */
void handleLongPress() {
    Serial.println("[Main] Long press detected - cycling mode");
    
    AppMode::Mode current = AppMode::getMode();
    AppMode::Mode next;
    
    // Cycle: User -> FieldAdmin -> Factory -> User
    switch (current) {
        case AppMode::Mode::User:
            next = AppMode::Mode::FieldAdmin;
            break;
        case AppMode::Mode::FieldAdmin:
            next = AppMode::Mode::Factory;
            break;
        case AppMode::Mode::Factory:
        default:
            next = AppMode::Mode::User;
            break;
    }
    
    // Force mode change (bypass password for hardware button)
    AppMode::forceMode(next);
    
    // Reset LED toggle state
    ledSteady = false;
    
    // Update LED to new mode's idle state
    switch (next) {
        case AppMode::Mode::User:
            StatusLED::setState(StatusLED::State::IdleUser);
            Serial.println("[Main] Mode: User (Blue pulse)");
            break;
        case AppMode::Mode::FieldAdmin:
            StatusLED::setState(StatusLED::State::IdleAdmin);
            Serial.println("[Main] Mode: FieldAdmin (Cyan pulse)");
            break;
        case AppMode::Mode::Factory:
            StatusLED::setState(StatusLED::State::IdleFactory);
            Serial.println("[Main] Mode: Factory (Magenta pulse)");
            break;
    }
}

/**
 * @brief Handle button input with debouncing and long press detection
 * 
 * Short press (<1s): Toggle LED or advance test state
 * Long press (≥1s): Cycle through operational modes
 */
void handleButtonPress() {
    bool currentState = (digitalRead(PIN_LOG_BUTTON) == BUTTON_ACTIVE_LEVEL);
    uint32_t now = millis();
    
    // Debounce
    if (currentState != lastButtonState) {
        lastButtonChangeMs = now;
        lastButtonState = currentState;
    }
    
    if ((now - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS) {
        // Button just pressed
        if (currentState && !buttonPressed) {
            buttonPressed = true;
            buttonPressStartMs = now;
            longPressTriggered = false;
        }
        
        // Button held - check for long press
        if (currentState && buttonPressed && !longPressTriggered) {
            if ((now - buttonPressStartMs) >= LONG_PRESS_MS) {
                longPressTriggered = true;
                handleLongPress();
            }
        }
        
        // Button released
        if (!currentState && buttonPressed) {
            if (!longPressTriggered) {
                handleShortPress();
            }
            buttonPressed = false;
        }
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Wait for USB CDC
    
    Serial.println();
    Serial.println("=====================================");
    Serial.println("  Loadcell Data Logger v1.0");
    Serial.println("  ESP32-S3 Platform");
    Serial.println("=====================================");
    Serial.println();
    
    // Initialize button pin
    pinMode(PIN_LOG_BUTTON, INPUT);
    Serial.println("[Main] Button initialized on GPIO " + String(PIN_LOG_BUTTON));
    
    // Initialize the status LED first for visual feedback
    StatusLED::init();
    StatusLED::setState(StatusLED::State::Init);
    
    // Initialize application mode (always starts in User mode)
    AppMode::init();
    
    // TODO: Initialize hardware drivers (ADC, IMU, RTC, SD)
    // TODO: Initialize calibration system
    
    // Initialize WebUI routes BEFORE starting WiFi
    // (Server will be started by WiFi event handler when AP is ready)
    if (!AdminWebUI::init()) {
        Serial.println("[Main] WebUI initialization failed");
        StatusLED::setState(StatusLED::State::ErrCritical);
    }
    
    // Start WiFi Access Point
    // The web server will be started from loop() when WiFi signals ready
    Serial.println("[Main] Starting WiFi AP...");
    if (WiFiAP::start()) {
        Serial.println("[Main] WiFi AP started: " + String(WiFiAP::getSSID()));
        Serial.println("[Main] Connect to: http://" + WiFiAP::getIP());
    } else {
        Serial.println("[Main] WiFi AP failed to start");
        StatusLED::setState(StatusLED::State::ErrCritical);
    }
    
    // Switch to User mode idle state (blue pulse - WiFi ON, waiting)
    StatusLED::setState(StatusLED::State::IdleUser);
    
    Serial.println();
    Serial.println("[Main] Setup complete");
    Serial.println("[Main] Mode: " + String(AppMode::getModeString()));
    Serial.println("[Main] LED: Blue pulsing (User mode idle)");
    Serial.println();
}

void loop() {
    // Start web server when WiFi AP is ready (deferred from event handler)
    // This must be done from the main loop context for TCP/IP stack safety
    if (!webServerStarted && WiFiAP::isReady()) {
        if (AdminWebUI::beginServer()) {
            Serial.println("[Main] Web server started successfully");
            webServerStarted = true;
        } else {
            Serial.println("[Main] Web server failed to start");
            StatusLED::setState(StatusLED::State::ErrCritical);
        }
    }
    
    // Update LED animation (must be called regularly)
    StatusLED::update();
    
    // Handle button input
    handleButtonPress();
    
    // TODO: State machine update
    
    delay(10); // Placeholder - will be removed when state machine is implemented
}


