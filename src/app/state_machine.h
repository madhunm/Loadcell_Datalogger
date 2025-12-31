/**
 * @file state_machine.h
 * @brief Main system state machine for coordinating all modules
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>
#include "../pin_config.h"
#include "../drivers/status_led.h"

// Forward declarations
class MAX11270Driver;
class LSM6DSVDriver;
class RX8900CEDriver;
class SDManager;
class WiFiAP;
class AdminWebUI;
class CalibrationStorage;
class CalibrationInterp;
class TimestampSync;
class LoggerModule;
class BinToCSVConverter;

/**
 * @brief Main system state machine
 * 
 * States:
 * - INIT: System initialization
 * - ADMIN: WiFi ON, WebUI active for calibration management
 * - PRELOG: Shutting down WiFi before logging
 * - LOGGING: Active data acquisition (WiFi OFF)
 * - STOPPING: Flushing buffers and closing files
 * - CONVERTING: Binary to CSV conversion
 * - READY: Conversion complete, SD safe to remove
 */
class StateMachine {
public:
    /**
     * @brief Initialize state machine and all subsystems
     * @return true if successful
     */
    bool begin();
    
    /**
     * @brief Update state machine (call from loop)
     */
    void update();
    
    /**
     * @brief Handle button press event
     */
    void handleButtonPress();
    
    /**
     * @brief Get current state
     */
    SystemState getCurrentState() const { return current_state; }
    
private:
    // Hardware drivers
    MAX11270Driver* adc;
    LSM6DSVDriver* imu;
    RX8900CEDriver* rtc;
    SDManager* sd;
    StatusLED* led;
    
    // Network
    WiFiAP* wifi;
    AdminWebUI* webui;
    
    // Calibration
    CalibrationStorage* cal_storage;
    CalibrationInterp* cal_interp;
    
    // Logging
    TimestampSync* timestamp_sync;
    LoggerModule* logger;
    BinToCSVConverter* converter;
    
    // State
    SystemState current_state;
    uint32_t state_enter_time;
    uint32_t last_button_time;
    String last_log_file;
    
    // Button handling
    bool button_pressed;
    bool button_was_pressed;
    
    /**
     * @brief Transition to new state
     */
    void changeState(SystemState new_state);
    
    /**
     * @brief State-specific update functions
     */
    void updateInit();
    void updateAdmin();
    void updatePreLog();
    void updateLogging();
    void updateStopping();
    void updateConverting();
    void updateReady();
    
    /**
     * @brief Read button state with debouncing
     */
    bool readButton();
    
    /**
     * @brief Initialize all hardware drivers
     */
    bool initHardware();
    
    /**
     * @brief Initialize all application modules
     */
    bool initModules();
};

#endif // STATE_MACHINE_H
