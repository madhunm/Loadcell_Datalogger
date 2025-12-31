/**
 * @file status_led.cpp
 * @brief NeoPixel Status LED Driver Implementation
 */

 #include "status_led.h"
 #include "../pin_config.h"
 #include <Adafruit_NeoPixel.h>
 
 namespace StatusLED {
 
 // ============================================================================
 // Private State
 // ============================================================================
 
 namespace {
     // NeoPixel instance
     Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
     
     // Current state
     State currentState = State::Off;
     Color currentColor = Colors::Off;
     Pattern currentPattern = Pattern::Off;
     
     // Animation timing
     uint32_t lastUpdateMs = 0;
     uint32_t animationPhase = 0;
     
     // Flash state
     bool isFlashing = false;
     State preFlashState = State::Off;
     uint32_t flashEndMs = 0;
     
     // Global brightness (0-255)
     uint8_t globalBrightness = 128;  // Default to 50% brightness
     
     // Animation timing constants (milliseconds)
     constexpr uint16_t PULSE_PERIOD_MS = 2000;          // 2 second breathing cycle
     constexpr uint16_t FAST_BLINK_PERIOD_MS = 200;      // 5Hz blink rate
     constexpr uint16_t VERY_FAST_BLINK_PERIOD_MS = 100; // 10Hz blink rate (critical)
     constexpr uint16_t UPDATE_INTERVAL_MS = 16;         // ~60 FPS update rate
     
     // Blink code timing constants
     constexpr uint16_t BLINK_ON_MS = 150;    // Duration LED is ON per blink
     constexpr uint16_t BLINK_OFF_MS = 150;   // Duration LED is OFF between blinks
     constexpr uint16_t BLINK_PAUSE_MS = 800; // Pause after blink sequence
     
     // Blink code state
     uint8_t blinkCodeCount = 0;       // Number of blinks for current error
     uint8_t blinkCodeCurrent = 0;     // Current blink number in sequence
     uint32_t blinkCodeStartMs = 0;    // Time when current blink/pause started
     bool blinkCodeLedOn = false;      // Current LED state in blink code
     
     // Lookup table for sine-based pulse (quarter wave, 0-255)
     // Generated from: round(255 * sin(i * PI / 2 / 64)) for i in 0..64
     constexpr uint8_t SINE_TABLE[65] = {
         0, 6, 13, 19, 25, 31, 37, 44, 50, 56, 62, 68, 74, 80, 86, 92,
         98, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176,
         181, 185, 189, 193, 197, 201, 205, 209, 212, 216, 219, 222, 225, 228, 231, 234,
         236, 238, 241, 243, 244, 246, 248, 249, 251, 252, 253, 254, 254, 255, 255, 255,
         255
     };
     
     /**
      * @brief Get sine wave value for animation (0-255)
      * @param phase Animation phase (0-65535 for full cycle)
      */
     uint8_t getSineValue(uint16_t phase) {
         // Map 16-bit phase to quarter-wave table index
         uint8_t quadrant = (phase >> 14) & 0x03;  // 0-3
         uint8_t index = (phase >> 8) & 0x3F;      // 0-63
         
         switch (quadrant) {
             case 0: return SINE_TABLE[index];           // Rising 0->255
             case 1: return SINE_TABLE[64 - index];      // Falling 255->0
             case 2: return 0;                            // Hold at 0 (modified breathing)
             case 3: return 0;                            // Hold at 0
             default: return 0;
         }
     }
     
     /**
      * @brief Get breathing pulse value (0-255)
      * 
      * Creates a smooth breathing effect with a natural pause at minimum.
      */
     uint8_t getBreathingValue(uint32_t timeMs) {
         // Full cycle over PULSE_PERIOD_MS
         uint32_t phase = ((timeMs % PULSE_PERIOD_MS) * 65536UL) / PULSE_PERIOD_MS;
         
         // Use modified sine for breathing: active half then pause
         uint8_t quadrant = (phase >> 14) & 0x03;
         uint8_t index = (phase >> 8) & 0x3F;
         
         if (quadrant == 0) {
             return SINE_TABLE[index];        // Rising 0->255
         } else if (quadrant == 1) {
             return SINE_TABLE[64 - index];   // Falling 255->0
         }
         return 0;  // Pause at minimum for second half
     }
     
     /**
      * @brief Apply brightness to a color component
      */
     uint8_t applyBrightness(uint8_t component, uint8_t brightness, uint8_t animValue) {
         uint32_t result = (uint32_t)component * brightness * animValue / (255UL * 255UL);
         return (uint8_t)result;
     }
     
     /**
      * @brief Get blink code animation value
      * 
      * Implements: N blinks (150ms on, 150ms off), then 800ms pause, repeat
      */
     uint8_t getBlinkCodeValue(uint32_t now) {
         uint32_t elapsed = now - blinkCodeStartMs;
         
         if (blinkCodeCurrent < blinkCodeCount) {
             // In blink sequence
             if (blinkCodeLedOn) {
                 // LED is ON, check if time to turn OFF
                 if (elapsed >= BLINK_ON_MS) {
                     blinkCodeLedOn = false;
                     blinkCodeStartMs = now;
                 }
                 return 255;
             } else {
                 // LED is OFF, check if time to turn ON (next blink)
                 if (elapsed >= BLINK_OFF_MS) {
                     blinkCodeCurrent++;
                     if (blinkCodeCurrent < blinkCodeCount) {
                         blinkCodeLedOn = true;
                         blinkCodeStartMs = now;
                         return 255;
                     }
                     // All blinks done, start pause
                     blinkCodeStartMs = now;
                 }
                 return 0;
             }
         } else {
             // In pause phase
             if (elapsed >= BLINK_PAUSE_MS) {
                 // Restart sequence
                 blinkCodeCurrent = 0;
                 blinkCodeLedOn = true;
                 blinkCodeStartMs = now;
                 return 255;
             }
             return 0;
         }
     }
     
     /**
      * @brief Update the physical NeoPixel with current color and animation
      */
     void updatePixel() {
         uint8_t r, g, b;
         uint8_t animValue = 255;  // Default full brightness for steady
         uint32_t now = millis();
         
         switch (currentPattern) {
             case Pattern::Off:
                 r = g = b = 0;
                 pixel.setPixelColor(0, pixel.Color(0, 0, 0));
                 pixel.show();
                 return;
                 
             case Pattern::Steady:
                 animValue = 255;
                 break;
                 
             case Pattern::Pulse:
                 animValue = getBreathingValue(now);
                 // Ensure minimum visibility during pulse (never go fully dark)
                 if (animValue < 30) animValue = 30;
                 break;
                 
             case Pattern::FastBlink:
                 animValue = ((now / (FAST_BLINK_PERIOD_MS / 2)) % 2) ? 255 : 0;
                 break;
                 
             case Pattern::VeryFastBlink:
                 animValue = ((now / (VERY_FAST_BLINK_PERIOD_MS / 2)) % 2) ? 255 : 0;
                 break;
                 
             case Pattern::BlinkCode:
                 animValue = getBlinkCodeValue(now);
                 break;
         }
         
         r = applyBrightness(currentColor.r, globalBrightness, animValue);
         g = applyBrightness(currentColor.g, globalBrightness, animValue);
         b = applyBrightness(currentColor.b, globalBrightness, animValue);
         
         pixel.setPixelColor(0, pixel.Color(r, g, b));
         pixel.show();
     }
     
     /**
      * @brief Initialize blink code sequence
      */
     void startBlinkCode(uint8_t count) {
         blinkCodeCount = count;
         blinkCodeCurrent = 0;
         blinkCodeLedOn = true;
         blinkCodeStartMs = millis();
     }
     
     /**
      * @brief Map system state to color and pattern
      */
     void applyStateMapping(State state) {
         switch (state) {
             case State::Off:
                 currentColor = Colors::Off;
                 currentPattern = Pattern::Off;
                 break;
                 
             case State::Init:
                 currentColor = Colors::Blue;
                 currentPattern = Pattern::Pulse;
                 break;
                 
             // Mode-specific idle states (WiFi ON)
             case State::IdleUser:
                 currentColor = Colors::Blue;
                 currentPattern = Pattern::Pulse;
                 break;
                 
             case State::IdleAdmin:
                 currentColor = Colors::Cyan;
                 currentPattern = Pattern::Pulse;
                 break;
                 
             case State::IdleFactory:
                 currentColor = Colors::Magenta;
                 currentPattern = Pattern::Pulse;
                 break;
                 
             // Operational states
             case State::Ready:
                 currentColor = Colors::Green;
                 currentPattern = Pattern::Steady;
                 break;
                 
             case State::Logging:
                 currentColor = Colors::Orange;
                 currentPattern = Pattern::Steady;
                 break;
                 
             case State::Stopping:
                 currentColor = Colors::Orange;
                 currentPattern = Pattern::FastBlink;
                 break;
                 
             case State::Converting:
                 currentColor = Colors::Magenta;
                 currentPattern = Pattern::Pulse;
                 break;
                 
             case State::FactoryTesting:
                 currentColor = Colors::Magenta;
                 currentPattern = Pattern::FastBlink;
                 break;
                 
             // Error states with blink codes
             case State::ErrSdMissing:
                 currentColor = Colors::Red;
                 currentPattern = Pattern::BlinkCode;
                 startBlinkCode(1);
                 break;
                 
             case State::ErrSdFull:
                 currentColor = Colors::Red;
                 currentPattern = Pattern::BlinkCode;
                 startBlinkCode(2);
                 break;
                 
             case State::ErrSdWrite:
                 currentColor = Colors::Red;
                 currentPattern = Pattern::BlinkCode;
                 startBlinkCode(3);
                 break;
                 
             case State::ErrAdc:
                 currentColor = Colors::Red;
                 currentPattern = Pattern::BlinkCode;
                 startBlinkCode(4);
                 break;
                 
             case State::ErrImu:
                 currentColor = Colors::Red;
                 currentPattern = Pattern::BlinkCode;
                 startBlinkCode(5);
                 break;
                 
             case State::ErrRtc:
                 currentColor = Colors::Red;
                 currentPattern = Pattern::BlinkCode;
                 startBlinkCode(6);
                 break;
                 
             case State::ErrCalibration:
                 currentColor = Colors::Magenta;
                 currentPattern = Pattern::FastBlink;
                 break;
                 
             case State::ErrCritical:
                 currentColor = Colors::Red;
                 currentPattern = Pattern::VeryFastBlink;
                 break;
         }
     }
     
 } // anonymous namespace
 
 // ============================================================================
 // Public API Implementation
 // ============================================================================
 
 bool init() {
     // Initialize the NeoPixel
     pixel.begin();
     pixel.setBrightness(255);  // We handle brightness ourselves for smoother animation
     pixel.clear();
     pixel.show();
     
     // Brief delay to let RMT driver initialize
     delay(10);
     
     // Startup test: flash sequence using outdoor-visible colors (no white/yellow)
     Serial.println("[StatusLED] Testing LED...");
     pixel.setPixelColor(0, pixel.Color(0, 0, 255));    // Blue
     pixel.show();
     delay(200);
     pixel.setPixelColor(0, pixel.Color(0, 255, 255));  // Cyan
     pixel.show();
     delay(200);
     pixel.setPixelColor(0, pixel.Color(0, 255, 0));    // Green
     pixel.show();
     delay(200);
     pixel.setPixelColor(0, pixel.Color(255, 100, 0));  // Orange
     pixel.show();
     delay(200);
     pixel.clear();
     pixel.show();
     
     currentState = State::Off;
     currentColor = Colors::Off;
     currentPattern = Pattern::Off;
     lastUpdateMs = millis();
     
     Serial.println("[StatusLED] Initialized on GPIO " + String(PIN_NEOPIXEL));
     return true;
 }
 
 void setState(State state) {
     if (state != currentState) {
         currentState = state;
         applyStateMapping(state);
         animationPhase = 0;  // Reset animation on state change
         
         // Debug output
         const char* stateName;
         switch (state) {
             case State::Off:            stateName = "Off"; break;
             case State::Init:           stateName = "Init"; break;
             case State::IdleUser:       stateName = "IdleUser"; break;
             case State::IdleAdmin:      stateName = "IdleAdmin"; break;
             case State::IdleFactory:    stateName = "IdleFactory"; break;
             case State::Ready:          stateName = "Ready"; break;
             case State::Logging:        stateName = "Logging"; break;
             case State::Stopping:       stateName = "Stopping"; break;
             case State::Converting:     stateName = "Converting"; break;
             case State::FactoryTesting: stateName = "FactoryTesting"; break;
             case State::ErrSdMissing:   stateName = "ErrSdMissing"; break;
             case State::ErrSdFull:      stateName = "ErrSdFull"; break;
             case State::ErrSdWrite:     stateName = "ErrSdWrite"; break;
             case State::ErrAdc:         stateName = "ErrAdc"; break;
             case State::ErrImu:         stateName = "ErrImu"; break;
             case State::ErrRtc:         stateName = "ErrRtc"; break;
             case State::ErrCalibration: stateName = "ErrCalibration"; break;
             case State::ErrCritical:    stateName = "ErrCritical"; break;
             default:                    stateName = "Unknown"; break;
         }
         Serial.println("[StatusLED] State: " + String(stateName));
         
         // Immediately update the pixel
         updatePixel();
     }
 }
 
State getState() {
    return currentState;
}

void setCustom(Color color, Pattern pattern) {
     currentColor = color;
     currentPattern = pattern;
     animationPhase = 0;
     updatePixel();
 }
 
 void setBrightness(uint8_t brightness) {
     globalBrightness = brightness;
     updatePixel();
 }
 
 void off() {
     setState(State::Off);
 }
 
void flash(Color color, uint16_t duration_ms) {
    if (!isFlashing) {
        preFlashState = currentState;
    }
    isFlashing = true;
    flashEndMs = millis() + duration_ms;
    
    // Apply flash color directly
    currentColor = color;
    currentPattern = Pattern::Steady;
    updatePixel();
}

// ============================================================================
// Factory Test Mode Implementation
// ============================================================================

namespace {
    // Test mode state
    bool testCycleActive = false;
    uint16_t testCycleIntervalMs = 1000;
    uint32_t testCycleLastChangeMs = 0;
    uint8_t testStateIndex = 0;
    
    // Define test states: color, pattern, blink count, name
    struct TestState {
        Color color;
        Pattern pattern;
        uint8_t blinkCount;
        const char* name;
    };
    
    const TestState TEST_STATES[] = {
        // Off state
        { Colors::Off,     Pattern::Off,           0, "Off" },
        
        // Solid colors
        { Colors::Red,     Pattern::Steady,        0, "Red Solid" },
        { Colors::Green,   Pattern::Steady,        0, "Green Solid" },
        { Colors::Blue,    Pattern::Steady,        0, "Blue Solid" },
        { Colors::Cyan,    Pattern::Steady,        0, "Cyan Solid" },
        { Colors::Orange,  Pattern::Steady,        0, "Orange Solid" },
        { Colors::Magenta, Pattern::Steady,        0, "Magenta Solid" },
        
        // Pulse patterns
        { Colors::Red,     Pattern::Pulse,         0, "Red Pulse" },
        { Colors::Green,   Pattern::Pulse,         0, "Green Pulse" },
        { Colors::Blue,    Pattern::Pulse,         0, "Blue Pulse" },
        { Colors::Cyan,    Pattern::Pulse,         0, "Cyan Pulse" },
        { Colors::Orange,  Pattern::Pulse,         0, "Orange Pulse" },
        { Colors::Magenta, Pattern::Pulse,         0, "Magenta Pulse" },
        
        // Fast blink patterns
        { Colors::Red,     Pattern::FastBlink,     0, "Red Fast Blink" },
        { Colors::Green,   Pattern::FastBlink,     0, "Green Fast Blink" },
        { Colors::Blue,    Pattern::FastBlink,     0, "Blue Fast Blink" },
        { Colors::Orange,  Pattern::FastBlink,     0, "Orange Fast Blink" },
        
        // Very fast blink (critical)
        { Colors::Red,     Pattern::VeryFastBlink, 0, "Red Very Fast (Critical)" },
        
        // Error blink codes (1-6 blinks)
        { Colors::Red,     Pattern::BlinkCode,     1, "Error Code 1 (SD Missing)" },
        { Colors::Red,     Pattern::BlinkCode,     2, "Error Code 2 (SD Full)" },
        { Colors::Red,     Pattern::BlinkCode,     3, "Error Code 3 (SD Write)" },
        { Colors::Red,     Pattern::BlinkCode,     4, "Error Code 4 (ADC)" },
        { Colors::Red,     Pattern::BlinkCode,     5, "Error Code 5 (IMU)" },
        { Colors::Red,     Pattern::BlinkCode,     6, "Error Code 6 (RTC)" },
    };
    
    constexpr uint8_t TEST_STATE_COUNT = sizeof(TEST_STATES) / sizeof(TEST_STATES[0]);
}

void setTestMode(Color color, Pattern pattern, uint8_t blinkCount) {
    // Stop auto-cycling if active
    testCycleActive = false;
    
    currentColor = color;
    currentPattern = pattern;
    
    if (pattern == Pattern::BlinkCode && blinkCount > 0) {
        startBlinkCode(blinkCount);
    }
    
    animationPhase = 0;
    updatePixel();
    
    Serial.printf("[StatusLED] Test mode: R=%d G=%d B=%d Pattern=%d BlinkCount=%d\n",
                  color.r, color.g, color.b, (int)pattern, blinkCount);
}

void nextTestState() {
    testStateIndex = (testStateIndex + 1) % TEST_STATE_COUNT;
    const TestState& state = TEST_STATES[testStateIndex];
    
    currentColor = state.color;
    currentPattern = state.pattern;
    
    if (state.pattern == Pattern::BlinkCode && state.blinkCount > 0) {
        startBlinkCode(state.blinkCount);
    }
    
    animationPhase = 0;
    updatePixel();
    
    Serial.printf("[StatusLED] Test state %d/%d: %s\n", 
                  testStateIndex + 1, TEST_STATE_COUNT, state.name);
}

void startTestCycle(uint16_t interval_ms) {
    testCycleActive = true;
    testCycleIntervalMs = interval_ms;
    testCycleLastChangeMs = millis();
    testStateIndex = 0;
    
    // Apply first state
    const TestState& state = TEST_STATES[0];
    currentColor = state.color;
    currentPattern = state.pattern;
    updatePixel();
    
    Serial.printf("[StatusLED] Test cycle started (interval=%dms, %d states)\n",
                  interval_ms, TEST_STATE_COUNT);
}

void stopTestCycle() {
    testCycleActive = false;
    Serial.println("[StatusLED] Test cycle stopped");
}

bool isTestCycling() {
    return testCycleActive;
}

uint8_t getTestStateIndex() {
    return testStateIndex;
}

uint8_t getTestStateCount() {
    return TEST_STATE_COUNT;
}

const char* getTestStateName() {
    if (testStateIndex < TEST_STATE_COUNT) {
        return TEST_STATES[testStateIndex].name;
    }
    return "Unknown";
}

// Override update to handle test cycling
void update() {
    uint32_t now = millis();
    
    // Handle test cycle auto-advance
    if (testCycleActive) {
        if (now - testCycleLastChangeMs >= testCycleIntervalMs) {
            testCycleLastChangeMs = now;
            nextTestState();
        }
    }
    
    // Handle flash timeout
    if (isFlashing && now >= flashEndMs) {
        isFlashing = false;
        setState(preFlashState);
        return;
    }
    
    // Rate limit updates for efficiency
    if (now - lastUpdateMs < UPDATE_INTERVAL_MS) {
        return;
    }
    lastUpdateMs = now;
    
    // Update animation
    updatePixel();
}

} // namespace StatusLED