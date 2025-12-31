/**
 * @file main.cpp
 * @brief ESP32-S3 Loadcell Data Logger - Main Entry Point
 * 
 * High-rate load cell + IMU data logging to SD card with:
 * - RTC-disciplined timestamps
 * - Button-controlled logging sessions
 * - NeoPixel status indication
 * - Admin WebUI for calibration management
 */

#include <Arduino.h>
#include "pin_config.h"
#include "app/state_machine.h"

// Global state machine instance
StateMachine state_machine;

void setup() {
    // Initialize serial
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Wait for USB CDC
    
    Serial.println();
    Serial.println("=====================================");
    Serial.println("  Loadcell Data Logger v1.0");
    Serial.println("  ESP32-S3 Platform");
    Serial.println("  Build: " __DATE__ " " __TIME__);
    Serial.println("=====================================");
    Serial.println();
    
    // Initialize state machine (handles all subsystems)
    if (!state_machine.begin()) {
        Serial.println();
        Serial.println("FATAL ERROR: Initialization failed!");
        Serial.println("System halted.");
        while (1) {
            delay(1000);
        }
    }
    
    Serial.println();
    Serial.println("=== System Ready ===");
    Serial.println();
}

void loop() {
    // Update state machine (handles everything)
    state_machine.update();
    
    // Small delay to prevent tight loop
    delay(10);
}
