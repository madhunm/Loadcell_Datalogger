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

// Blinking state for error patterns
static bool blinkOn = false;
static uint32_t lastBlinkChange = 0;

// Per-pattern blink configuration
static uint32_t blinkOnMs = 0;
static uint32_t blinkOffMs = 0;
static uint8_t blinkPulsesPerGroup = 0;
static uint8_t blinkCurrentPulse = 0;
static uint32_t blinkGroupGapMs = 0;

// Apply base colour immediately (no blinking logic)
static void applyStaticColour()
{
    pixel.setPixelColor(0, pixel.Color(baseR, baseG, baseB));
    pixel.show();
}

// Configure blink parameters for each error type
static void configureErrorPattern(NeopixelPattern pattern)
{
    // Defaults (will be overridden)
    blinkOnMs = 200;
    blinkOffMs = 200;
    blinkPulsesPerGroup = 1;
    blinkGroupGapMs = 800;

    switch (pattern)
    {
    case NEOPIXEL_PATTERN_ERROR_SD:
        // SD error: bright red, double-blink
        baseR = 255;
        baseG = 0;
        baseB = 0;
        blinkOnMs = 100;
        blinkOffMs = 100;
        blinkPulsesPerGroup = 2;
        blinkGroupGapMs = 600;
        break;

    case NEOPIXEL_PATTERN_ERROR_RTC:
        // RTC error: yellow, single short blink per second
        baseR = 255;
        baseG = 255;
        baseB = 0;
        blinkOnMs = 200;
        blinkOffMs = 800;
        blinkPulsesPerGroup = 1;
        blinkGroupGapMs = 0; // handled by off duration
        break;

    case NEOPIXEL_PATTERN_ERROR_IMU:
        // IMU error: magenta, triple fast blips
        baseR = 255;
        baseG = 0;
        baseB = 255;
        blinkOnMs = 80;
        blinkOffMs = 80;
        blinkPulsesPerGroup = 3;
        blinkGroupGapMs = 600;
        break;

    case NEOPIXEL_PATTERN_ERROR_ADC:
        // ADC error: cyan, long pulse
        baseR = 0;
        baseG = 255;
        baseB = 255;
        blinkOnMs = 500;
        blinkOffMs = 500;
        blinkPulsesPerGroup = 1;
        blinkGroupGapMs = 0;
        break;

    default:
        // Should not happen for non-error patterns
        baseR = baseG = baseB = 0;
        blinkOnMs = 200;
        blinkOffMs = 200;
        blinkPulsesPerGroup = 1;
        blinkGroupGapMs = 800;
        break;
    }

    blinkOn = true;
    blinkCurrentPulse = 0;
    lastBlinkChange = millis();

    // Turn LED on immediately
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
        // INIT: solid amber (red+green) during peripheral bring-up
        baseR = 255;
        baseG = 80;
        baseB = 0;
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_READY:
        // READY: solid green
        baseR = 0;
        baseG = 255;
        baseB = 0;
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_LOGGING:
        // LOGGING: solid green for now (can become breathing later)
        baseR = 0;
        baseG = 255;
        baseB = 0;
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_CONVERTING:
        // CONVERTING: orange/yellow blinking (not safe to remove SD card)
        baseR = 255;
        baseG = 100;
        baseB = 0;
        blinkOnMs = 150;
        blinkOffMs = 150;
        blinkPulsesPerGroup = 1;
        blinkGroupGapMs = 0;
        blinkOn = true;
        blinkCurrentPulse = 0;
        lastBlinkChange = millis();
        pixel.setPixelColor(0, pixel.Color(baseR, baseG, baseB));
        pixel.show();
        break;

    case NEOPIXEL_PATTERN_SAFE_TO_REMOVE:
        // SAFE_TO_REMOVE: green double-blink pattern (safe to remove SD card)
        baseR = 0;
        baseG = 255;
        baseB = 0;
        blinkOnMs = 100;
        blinkOffMs = 100;
        blinkPulsesPerGroup = 2;
        blinkGroupGapMs = 800;
        blinkOn = true;
        blinkCurrentPulse = 0;
        lastBlinkChange = millis();
        pixel.setPixelColor(0, pixel.Color(baseR, baseG, baseB));
        pixel.show();
        break;

    case NEOPIXEL_PATTERN_ERROR_SD:
    case NEOPIXEL_PATTERN_ERROR_RTC:
    case NEOPIXEL_PATTERN_ERROR_IMU:
    case NEOPIXEL_PATTERN_ERROR_ADC:
        // Error patterns: blinking with different colours/signatures
        configureErrorPattern(currentPattern);
        break;
    }
}

void neopixelUpdate()
{
    // Patterns with time-based animation (blinking)
    if (currentPattern != NEOPIXEL_PATTERN_ERROR_SD &&
        currentPattern != NEOPIXEL_PATTERN_ERROR_RTC &&
        currentPattern != NEOPIXEL_PATTERN_ERROR_IMU &&
        currentPattern != NEOPIXEL_PATTERN_ERROR_ADC &&
        currentPattern != NEOPIXEL_PATTERN_CONVERTING &&
        currentPattern != NEOPIXEL_PATTERN_SAFE_TO_REMOVE)
    {
        return;
    }

    uint32_t now = millis();

    if (blinkOn)
    {
        if (now - lastBlinkChange >= blinkOnMs)
        {
            blinkOn = false;
            lastBlinkChange = now;
            pixel.setPixelColor(0, pixel.Color(0, 0, 0));
            pixel.show();
        }
    }
    else
    {
        // LED is currently off; decide when to turn back on
        uint32_t interval = blinkOffMs;

        if (blinkPulsesPerGroup > 1 && blinkCurrentPulse >= blinkPulsesPerGroup)
        {
            // Between groups, use the group gap
            interval = blinkGroupGapMs;
        }

        if (now - lastBlinkChange >= interval)
        {
            lastBlinkChange = now;

            if (blinkPulsesPerGroup > 1 && blinkCurrentPulse >= blinkPulsesPerGroup)
            {
                blinkCurrentPulse = 0;
            }

            blinkOn = true;
            blinkCurrentPulse++;
            pixel.setPixelColor(0, pixel.Color(baseR, baseG, baseB));
            pixel.show();
        }
    }
}
