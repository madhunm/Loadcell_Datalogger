/**
 * @file status_led.h
 * @brief NeoPixel Status LED Driver for ESP32-S3 Loadcell Data Logger
 * 
 * Provides visual feedback for system states using a single WS2812B NeoPixel.
 * Each system state maps to a unique color and animation pattern.
 * Optimized for outdoor visibility - no white/yellow colors.
 * 
 * Mode-Specific Idle States (WiFi ON):
 *   - IdleUser:       Blue pulse (User mode, normal operation)
 *   - IdleAdmin:      Cyan pulse (Field Admin mode, configuration)
 *   - IdleFactory:    Magenta pulse (Factory mode, testing)
 * 
 * Operational States:
 *   - Init:           Blue pulse (startup)
 *   - Ready:          GREEN SOLID (ready to log)
 *   - Logging:        ORANGE SOLID (active acquisition)
 *   - Stopping:       Orange fast blink (flushing buffers)
 *   - Converting:     Magenta pulse (binary to CSV)
 *   - FactoryTesting: Magenta fast blink (running factory tests)
 * 
 * Error States (Red blink codes):
 *   - ErrSdMissing:   Red 1 blink  (SD card not found)
 *   - ErrSdFull:      Red 2 blinks (SD card full)
 *   - ErrSdWrite:     Red 3 blinks (SD write error)
 *   - ErrAdc:         Red 4 blinks (ADC failure)
 *   - ErrImu:         Red 5 blinks (IMU failure)
 *   - ErrRtc:         Red 6 blinks (RTC failure)
 *   - ErrCalibration: Magenta fast blink (no calibration data)
 *   - ErrCritical:    Red very fast blink (critical error)
 */

 #ifndef STATUS_LED_H
 #define STATUS_LED_H
 
 #include <Arduino.h>
 
 namespace StatusLED {
 
 /**
  * @brief System states that map to LED patterns
  */
 enum class State : uint8_t {
     Off,             ///< LED off
     Init,            ///< Blue pulse - system initializing
     // Mode-specific idle states (WiFi ON, waiting for action)
     IdleUser,        ///< Blue pulse - User mode idle
     IdleAdmin,       ///< Cyan pulse - Field Admin mode idle
     IdleFactory,     ///< Magenta pulse - Factory mode idle
     // Operational states
     Ready,           ///< GREEN SOLID - ready to start logging
     Logging,         ///< ORANGE SOLID - active data acquisition
     Stopping,        ///< Orange fast blink - flushing buffers
     Converting,      ///< Magenta pulse - binary to CSV conversion
     FactoryTesting,  ///< Magenta fast blink - running factory tests
     // Error states with blink codes
     ErrSdMissing,    ///< Red 1 blink - SD card not found
     ErrSdFull,       ///< Red 2 blinks - SD card full
     ErrSdWrite,      ///< Red 3 blinks - SD write error
     ErrAdc,          ///< Red 4 blinks - ADC sensor failure
     ErrImu,          ///< Red 5 blinks - IMU sensor failure
     ErrRtc,          ///< Red 6 blinks - RTC failure
     ErrCalibration,  ///< Magenta fast blink - no calibration data
     ErrCritical      ///< Red very fast blink - critical/multiple errors
 };
 
 /**
  * @brief Animation pattern types
  */
 enum class Pattern : uint8_t {
     Off,         ///< LED completely off
     Steady,      ///< Constant brightness
     Pulse,       ///< Smooth breathing effect (sine wave)
     FastBlink,   ///< Rapid on/off blinking (5Hz)
     VeryFastBlink, ///< Very rapid blinking (10Hz) for critical errors
     BlinkCode    ///< N blinks then pause (for error codes)
 };
 
 /**
  * @brief RGB color structure
  */
 struct Color {
     uint8_t r;
     uint8_t g;
     uint8_t b;
     
     constexpr Color(uint8_t red, uint8_t green, uint8_t blue) 
         : r(red), g(green), b(blue) {}
 };
 
 // Predefined colors (outdoor-optimized, no white/yellow)
 namespace Colors {
     constexpr Color Off      {  0,   0,   0};
     constexpr Color Red      {255,   0,   0};
     constexpr Color Green    {  0, 255,   0};
     constexpr Color Blue     {  0,   0, 255};
     constexpr Color Cyan     {  0, 255, 255};
     constexpr Color Orange   {255, 100,   0};
     constexpr Color Magenta  {255,   0, 255};
 }
 
 /**
  * @brief Initialize the NeoPixel LED
  * 
  * Configures the GPIO and NeoPixel library.
  * Must be called before any other StatusLED functions.
  * 
  * @return true if initialization successful
  */
 bool init();
 
 /**
  * @brief Set the LED state
  * 
  * Maps system state to appropriate color and animation pattern.
  * The LED will display the pattern until the state changes.
  * 
  * @param state The new system state
  */
 void setState(State state);
 
 /**
  * @brief Get the current LED state
  * 
  * @return Current State enum value
  */
 State getState();
 
 /**
  * @brief Update the LED animation
  * 
  * Must be called regularly from the main loop (ideally every frame).
  * Handles animation timing and NeoPixel updates.
  * Non-blocking - returns immediately.
  */
 void update();
 
 /**
  * @brief Set a custom color and pattern
  * 
  * For advanced use cases not covered by the standard states.
  * 
  * @param color RGB color
  * @param pattern Animation pattern
  */
 void setCustom(Color color, Pattern pattern);
 
 /**
  * @brief Set LED brightness
  * 
  * Global brightness scale applied to all colors.
  * 
  * @param brightness 0-255 (0=off, 255=full brightness)
  */
 void setBrightness(uint8_t brightness);
 
 /**
  * @brief Turn off the LED immediately
  * 
  * Sets state to Off and updates the LED.
  */
 void off();
 
/**
  * @brief Flash a color briefly (non-blocking)
  * 
  * Useful for event indication (button press, etc).
  * After the flash duration, returns to the previous state.
  * 
  * @param color Color to flash
  * @param duration_ms Duration in milliseconds
  */
 void flash(Color color, uint16_t duration_ms = 100);

 // ========================================================================
 // Factory Test Mode Functions
 // ========================================================================

 /**
  * @brief Set LED color and pattern directly (for factory testing)
  * 
  * @param color Color to display
  * @param pattern Pattern to use
  * @param blinkCount For BlinkCode pattern, number of blinks (1-6)
  */
 void setTestMode(Color color, Pattern pattern, uint8_t blinkCount = 1);

 /**
  * @brief Advance to the next test state
  * 
  * Cycles through all colors and patterns for visual inspection.
  * Order: Off, Colors (solid), Colors (pulse), Blink codes 1-6, Fast patterns
  */
 void nextTestState();

 /**
  * @brief Start automatic test cycling
  * 
  * @param interval_ms Time between state changes in milliseconds
  */
 void startTestCycle(uint16_t interval_ms = 1000);

 /**
  * @brief Stop automatic test cycling
  */
 void stopTestCycle();

 /**
  * @brief Check if auto-cycling is active
  * 
  * @return true if cycling
  */
 bool isTestCycling();

 /**
  * @brief Get the current test state index
  * 
  * @return Current index in the test sequence
  */
 uint8_t getTestStateIndex();

 /**
  * @brief Get total number of test states
  * 
  * @return Number of states in test sequence
  */
 uint8_t getTestStateCount();

 /**
  * @brief Get name of current test state
  * 
  * @return String description of current test state
  */
 const char* getTestStateName();

 } // namespace StatusLED
 
 #endif // STATUS_LED_H