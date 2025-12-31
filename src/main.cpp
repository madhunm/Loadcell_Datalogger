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

// Button state for debouncing
namespace {
    bool lastButtonState = false;
    uint32_t lastButtonChangeMs = 0;
    bool buttonPressed = false;
}

/**
 * @brief Handle button press for Factory mode LED cycling
 * 
 * In Factory mode, button press advances to next LED test state.
 * In other modes, button will control logging start/stop.
 */
void handleButtonPress() {
    // Read current button state
    bool currentState = (digitalRead(PIN_LOG_BUTTON) == BUTTON_ACTIVE_LEVEL);
    uint32_t now = millis();
    
    // Debounce
    if (currentState != lastButtonState) {
        lastButtonChangeMs = now;
        lastButtonState = currentState;
    }
    
    if ((now - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS) {
        // Detect rising edge (button just pressed)
        if (currentState && !buttonPressed) {
            buttonPressed = true;
            
            // In Factory mode, advance LED test state
            if (AppMode::getMode() == AppMode::Mode::Factory) {
                Serial.println("[Main] Button: Advancing LED test state");
                StatusLED::nextTestState();
            } else {
                // TODO: Handle logging start/stop in other modes
                Serial.println("[Main] Button pressed (mode: " + String(AppMode::getModeString()) + ")");
            }
        }
        else if (!currentState && buttonPressed) {
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
    
    // Start WiFi Access Point
    Serial.println("[Main] Starting WiFi AP...");
    if (WiFiAP::start()) {
        Serial.println("[Main] WiFi AP started: " + String(WiFiAP::getSSID()));
        Serial.println("[Main] Connect to: http://" + WiFiAP::getIP());
        
        // Start WebUI server
        if (AdminWebUI::init()) {
            Serial.println("[Main] WebUI server started");
        } else {
            Serial.println("[Main] WebUI server failed to start");
            StatusLED::setState(StatusLED::State::ErrCritical);
        }
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
    // Update LED animation (must be called regularly)
    StatusLED::update();
    
    // Handle button input
    handleButtonPress();
    
    // TODO: State machine update
    
    delay(10); // Placeholder - will be removed when state machine is implemented
}

