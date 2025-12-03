#include "neopixel.h"

#include <Adafruit_NeoPixel.h>
#include "pins.h"

#define NEOPIXEL_NUMPIXELS 1

static Adafruit_NeoPixel pixel(NEOPIXEL_NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static NeopixelPattern currentPattern = NEOPIXEL_PATTERN_OFF;

// Internal helper to apply the current pattern as a static colour.
// Later we can make this time-dependent (blinking, breathing etc.).
static void applyPattern()
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (currentPattern)
    {
    case NEOPIXEL_PATTERN_OFF:
        r = g = b = 0;
        break;

    case NEOPIXEL_PATTERN_INIT:
        // Red during SD/RTC/IMU/ADC initialisation
        r = 255;
        g = 0;
        b = 0;
        break;

    case NEOPIXEL_PATTERN_READY:
        // Solid green when system is ready to log
        r = 0;
        g = 255;
        b = 0;
        break;

    case NEOPIXEL_PATTERN_LOGGING:
        // For now also green; we can change this to blinking or another
        // colour later without touching the rest of the code.
        r = 0;
        g = 255;
        b = 0;
        break;

    case NEOPIXEL_PATTERN_ERROR:
        // Solid blue for "something went wrong"
        r = 0;
        g = 0;
        b = 255;
        break;
    }

    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

void neopixelInit()
{
    pixel.begin();
    pixel.setBrightness(50); // tweak if needed
    currentPattern = NEOPIXEL_PATTERN_OFF;
    applyPattern();
}

void neopixelSetPattern(NeopixelPattern pattern)
{
    if (pattern == currentPattern)
    {
        return; // nothing to do
    }

    currentPattern = pattern;
    applyPattern();
}

void neopixelUpdate()
{
    // Placeholder for future animations.
    // Right now patterns are static colours, so nothing to do here.
    // If we later add blinking / breathing, we’ll use millis() here.
}
