#include "neopixel.h"

#include <Adafruit_NeoPixel.h>
#include "pins.h"

#define NEOPIXEL_NUMPIXELS 1

static Adafruit_NeoPixel pixel(NEOPIXEL_NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static NeopixelPattern currentPattern = NEOPIXEL_PATTERN_OFF;

// Base colour for the current pattern
static uint8_t baseR = 0;
static uint8_t baseG = 0;
static uint8_t baseB = 0;

// Blinking state for ERROR pattern
static bool errorLedOn = false;
static uint32_t errorLastToggle = 0;
static const uint32_t ERROR_BLINK_INTERVAL_MS = 250; // 4 Hz blink (on/off every 250 ms)

// Apply base colour immediately (no blinking logic)
static void applyStaticColour()
{
    pixel.setPixelColor(0, pixel.Color(baseR, baseG, baseB));
    pixel.show();
}

void neopixelInit()
{
    pixel.begin();
    pixel.setBrightness(50);
    currentPattern = NEOPIXEL_PATTERN_OFF;
    baseR = baseG = baseB = 0;
    applyStaticColour();
}

void neopixelSetPattern(NeopixelPattern pattern)
{
    currentPattern = pattern;

    switch (currentPattern)
    {
    case NEOPIXEL_PATTERN_OFF:
        baseR = baseG = baseB = 0;
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_INIT:
        // Red during SD/RTC/IMU/ADC initialisation
        baseR = 255;
        baseG = 0;
        baseB = 0;
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_READY:
        // Solid green when system is ready to log
        baseR = 0;
        baseG = 255;
        baseB = 0;
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_LOGGING:
        // For now also solid green; we can change to a breathing pattern later.
        baseR = 0;
        baseG = 255;
        baseB = 0;
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_ERROR:
        // Error base colour: blue, but blinking handled in neopixelUpdate().
        baseR = 0;
        baseG = 0;
        baseB = 255;
        errorLedOn = true;
        errorLastToggle = millis();
        // Turn it on immediately
        applyStaticColour();
        break;
    }
}

void neopixelUpdate()
{
    // Only ERROR pattern currently has a time-based animation
    if (currentPattern != NEOPIXEL_PATTERN_ERROR)
    {
        return;
    }

    uint32_t now = millis();
    if (now - errorLastToggle >= ERROR_BLINK_INTERVAL_MS)
    {
        errorLastToggle = now;
        errorLedOn = !errorLedOn;

        if (errorLedOn)
        {
            pixel.setPixelColor(0, pixel.Color(baseR, baseG, baseB));
        }
        else
        {
            pixel.setPixelColor(0, pixel.Color(0, 0, 0));
        }
        pixel.show();
    }
}
