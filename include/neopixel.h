#pragma once

#include <Arduino.h>

// High-level patterns for system + per-peripheral errors
enum NeopixelPattern
{
    NEOPIXEL_PATTERN_OFF = 0,
    NEOPIXEL_PATTERN_INIT,    // board powering up / peripherals initialising
    NEOPIXEL_PATTERN_READY,   // ready to log
    NEOPIXEL_PATTERN_LOGGING, // actively logging
    NEOPIXEL_PATTERN_CONVERTING,  // converting binary logs to CSV (not safe to remove SD)
    NEOPIXEL_PATTERN_SAFE_TO_REMOVE, // CSV conversion complete (safe to remove SD)

    NEOPIXEL_PATTERN_ERROR_SD,  // SD card error
    NEOPIXEL_PATTERN_ERROR_RTC, // RTC error
    NEOPIXEL_PATTERN_ERROR_IMU, // IMU error
    NEOPIXEL_PATTERN_ERROR_ADC  // ADC error
};

// Must be called once at startup
void neopixelInit();

// Set the high-level pattern; the implementation decides colours/blink style.
void neopixelSetPattern(NeopixelPattern pattern);

// Call from loop() to update animations (blinking, etc.)
void neopixelUpdate();
