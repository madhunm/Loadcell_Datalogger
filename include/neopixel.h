#pragma once

#include <Arduino.h>

// Simple patterns we can expand later
enum NeopixelPattern
{
    NEOPIXEL_PATTERN_OFF = 0,
    NEOPIXEL_PATTERN_INIT,    // board powering up / peripherals initialising
    NEOPIXEL_PATTERN_READY,   // ready to log
    NEOPIXEL_PATTERN_LOGGING, // actively logging
    NEOPIXEL_PATTERN_ERROR    // error state
};

// Must be called once at startup
void neopixelInit();

// Set the high-level pattern; the implementation decides colours/blink style
void neopixelSetPattern(NeopixelPattern pattern);

// Call from loop() to update animations (for now it just holds a colour,
// but later we can add blinking / breathing etc.)
void neopixelUpdate();
