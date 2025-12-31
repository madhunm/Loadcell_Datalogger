/**
 * @file status_led.cpp
 * @brief Implementation of NeoPixel status LED controller
 */

#include "status_led.h"

bool StatusLED::begin() {
    pixel = Adafruit_NeoPixel(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
    pixel.begin();
    pixel.setBrightness(50);  // 50/255 = ~20% brightness
    pixel.clear();
    pixel.show();
    
    current_state = STATE_INIT;
    last_update = 0;
    pulse_value = 0;
    pulse_direction = true;
    
    return true;
}

void StatusLED::setState(SystemState state) {
    current_state = state;
    pulse_value = 0;
    pulse_direction = true;
}

uint8_t StatusLED::getPulseValue(uint32_t period_ms) {
    uint32_t now = millis();
    uint32_t phase = now % period_ms;
    
    // Sine-like pulse using triangle wave
    if (phase < period_ms / 2) {
        return (phase * 255) / (period_ms / 2);
    } else {
        return 255 - ((phase - period_ms / 2) * 255) / (period_ms / 2);
    }
}

bool StatusLED::getBlinkValue(uint32_t period_ms) {
    uint32_t now = millis();
    return ((now / period_ms) % 2) == 0;
}

uint32_t StatusLED::getStateColor() {
    uint8_t brightness;
    
    switch (current_state) {
        case STATE_INIT:
            // White pulse (1 second period)
            brightness = getPulseValue(1000);
            return pixel.Color(brightness, brightness, brightness);
            
        case STATE_ADMIN:
            // Cyan pulse (2 second period)
            brightness = getPulseValue(2000);
            return pixel.Color(0, brightness, brightness);
            
        case STATE_PRELOG:
            // Yellow fast blink (200ms period)
            brightness = getBlinkValue(200) ? 255 : 0;
            return pixel.Color(brightness, brightness, 0);
            
        case STATE_LOGGING:
            // Green pulse (1.5 second period)
            brightness = getPulseValue(1500);
            return pixel.Color(0, brightness, 0);
            
        case STATE_STOPPING:
            // Yellow pulse (1 second period)
            brightness = getPulseValue(1000);
            return pixel.Color(brightness, brightness, 0);
            
        case STATE_CONVERTING:
            // Orange pulse (1 second period)
            brightness = getPulseValue(1000);
            return pixel.Color(brightness, brightness / 2, 0);
            
        case STATE_READY:
            // Green steady
            return pixel.Color(0, 128, 0);
            
        case STATE_ERROR:
            // Red fast blink (300ms period)
            brightness = getBlinkValue(300) ? 255 : 0;
            return pixel.Color(brightness, 0, 0);
            
        default:
            return pixel.Color(0, 0, 0);
    }
}

void StatusLED::update() {
    uint32_t now = millis();
    
    // Update at 20Hz for smooth animations
    if (now - last_update >= 50) {
        last_update = now;
        
        uint32_t color = getStateColor();
        pixel.setPixelColor(0, color);
        pixel.show();
    }
}

void StatusLED::off() {
    pixel.clear();
    pixel.show();
}

void StatusLED::setColor(uint8_t r, uint8_t g, uint8_t b) {
    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}
