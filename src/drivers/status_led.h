/**
 * @file status_led.h
 * @brief NeoPixel status LED control with state-based patterns
 */

#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "../pin_config.h"

/**
 * @brief System states with corresponding LED patterns
 */
enum SystemState {
    STATE_INIT,         ///< Initializing - white pulse
    STATE_ADMIN,        ///< Admin/WiFi mode - cyan pulse
    STATE_PRELOG,       ///< Preparing to log - yellow fast blink
    STATE_LOGGING,      ///< Active logging - green pulse
    STATE_STOPPING,     ///< Flushing buffers - yellow pulse
    STATE_CONVERTING,   ///< Binary to CSV - orange pulse
    STATE_READY,        ///< Ready/SD safe - green steady
    STATE_ERROR         ///< Error - red fast blink
};

/**
 * @brief NeoPixel status LED controller with automatic pattern updates
 */
class StatusLED {
public:
    /**
     * @brief Initialize the status LED
     * @return true if successful
     */
    bool begin();
    
    /**
     * @brief Set the current system state (changes LED pattern)
     * @param state New system state
     */
    void setState(SystemState state);
    
    /**
     * @brief Update LED pattern (call regularly from loop)
     * Should be called at least every 50ms for smooth animations
     */
    void update();
    
    /**
     * @brief Turn LED off
     */
    void off();
    
    /**
     * @brief Set custom color (overrides state pattern until setState called)
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * @brief Get current system state
     * @return Current state
     */
    SystemState getState() const { return current_state; }
    
private:
    Adafruit_NeoPixel pixel;
    SystemState current_state;
    uint32_t last_update;
    uint8_t pulse_value;
    bool pulse_direction;
    
    /**
     * @brief Calculate color for current state and animation frame
     */
    uint32_t getStateColor();
    
    /**
     * @brief Pulse animation helper (0-255)
     */
    uint8_t getPulseValue(uint32_t period_ms);
    
    /**
     * @brief Blink animation helper (on/off)
     */
    bool getBlinkValue(uint32_t period_ms);
};

#endif // STATUS_LED_H
