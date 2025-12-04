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
        // RTC error: Yellow/Amber (ANSI Z535.1 - Warning)
        baseR = 255;
        baseG = 165;
        baseB = 0;
        blinkOnMs = 200;
        blinkOffMs = 800;
        blinkPulsesPerGroup = 1;
        blinkGroupGapMs = 0; // handled by off duration
        break;

    case NEOPIXEL_PATTERN_ERROR_IMU:
        // IMU error: Red (ANSI Z535.1 - Emergency/Hazard)
        baseR = 255;
        baseG = 0;
        baseB = 0;
        blinkOnMs = 100;
        blinkOffMs = 100;
        blinkPulsesPerGroup = 3;
        blinkGroupGapMs = 600;
        break;

    case NEOPIXEL_PATTERN_ERROR_ADC:
        // ADC error: Red (ANSI Z535.1 - Emergency/Hazard)
        baseR = 255;
        baseG = 0;
        baseB = 0;
        blinkOnMs = 200;
        blinkOffMs = 300;
        blinkPulsesPerGroup = 2;
        blinkGroupGapMs = 500;
        break;

    case NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE:
        // Write failure: bright red, very fast blink (critical error)
        baseR = 255;
        baseG = 0;
        baseB = 0;
        blinkOnMs = 50;
        blinkOffMs = 50;
        blinkPulsesPerGroup = 1;
        blinkGroupGapMs = 0;
        break;

    case NEOPIXEL_PATTERN_ERROR_LOW_SPACE:
        // Low SD card space: orange/yellow, slow double-blink
        baseR = 255;
        baseG = 165;
        baseB = 0;
        blinkOnMs = 200;
        blinkOffMs = 200;
        blinkPulsesPerGroup = 2;
        blinkGroupGapMs = 400;
        break;

    case NEOPIXEL_PATTERN_ERROR_BUFFER_FULL:
        // Buffer overflow: Red (ANSI Z535.1 - Emergency/Hazard)
        baseR = 255;
        baseG = 0;
        baseB = 0;
        blinkOnMs = 100;
        blinkOffMs = 100;
        blinkPulsesPerGroup = 3;
        blinkGroupGapMs = 400;
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
        // OFF: White (ANSI Z535.1 - General Information) or off
        baseR = baseG = baseB = 0;  // Actually off, not white
        applyStaticColour();
        break;

    case NEOPIXEL_PATTERN_INIT:
        // INIT: Blue (ANSI Z535.1 - Mandatory Action/User Required)
        baseR = 0;
        baseG = 128;
        baseB = 255;
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
        // CONVERTING: Yellow/Amber (ANSI Z535.1 - Warning, not safe to remove SD card)
        baseR = 255;
        baseG = 165;
        baseB = 0;
        blinkOnMs = 250;  // Slow flash: ~2 Hz (120/min) - medium priority
        blinkOffMs = 250;
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
    case NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE:
    case NEOPIXEL_PATTERN_ERROR_LOW_SPACE:
    case NEOPIXEL_PATTERN_ERROR_BUFFER_FULL:
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
        currentPattern != NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE &&
        currentPattern != NEOPIXEL_PATTERN_ERROR_LOW_SPACE &&
        currentPattern != NEOPIXEL_PATTERN_ERROR_BUFFER_FULL &&
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
