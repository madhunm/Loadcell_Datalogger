/**
 * @file state_machine.h
 * @brief System State Machine
 * 
 * Manages system states and transitions for the loadcell datalogger:
 *   Init -> Admin -> PreLog -> Logging -> Stopping -> Converting -> Ready
 *                         \              ^
 *                          \--> Error --/
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>

namespace StateMachine {

// ============================================================================
// States
// ============================================================================

/** @brief System states */
enum class State {
    Init,           // Hardware initialization
    Admin,          // WiFi ON, WebUI active (idle)
    PreLog,         // Preparing to log (WiFi shutdown)
    Logging,        // Active data acquisition
    Stopping,       // Flushing buffers, closing file
    Converting,     // Binary to CSV conversion
    Ready,          // SD card safe to remove
    Error           // Error state (recoverable)
};

// ============================================================================
// Events
// ============================================================================

/** @brief State machine events */
enum class Event {
    InitComplete,       // Initialization finished
    ButtonShort,        // Short button press
    ButtonLong,         // Long button press
    LogStarted,         // Logging started
    LogStopped,         // Logging stopped
    ConvertStarted,     // Conversion started
    ConvertComplete,    // Conversion finished
    SdRemoved,          // SD card removed
    SdInserted,         // SD card inserted
    Error,              // Error occurred
    ErrorCleared,       // Error condition cleared
    Timeout,            // State timeout
    AdminMode,          // Enter admin mode
    ExitAdmin           // Exit admin mode
};

/** @brief Error codes */
enum class ErrorCode {
    None = 0,
    SdMissing,
    SdFull,
    SdWriteError,
    AdcError,
    ImuError,
    RtcError,
    CalibrationMissing,
    BufferOverflow,
    Critical
};

// ============================================================================
// Status
// ============================================================================

/** @brief State machine status */
struct Status {
    State state;
    State previousState;
    uint32_t stateEntryMs;
    uint32_t stateDurationMs;
    ErrorCode lastError;
    uint32_t errorCount;
    bool sdCardPresent;
    bool wifiActive;
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize state machine
 * 
 * Sets initial state to Init.
 */
void init();

/**
 * @brief Get current state
 */
State getState();

/**
 * @brief Get state name as string
 */
const char* getStateName();

/**
 * @brief Get state name for specific state
 */
const char* getStateName(State state);

/**
 * @brief Process an event
 * 
 * Triggers state transitions based on current state and event.
 * 
 * @param event Event to process
 */
void processEvent(Event event);

/**
 * @brief Handle button press
 * 
 * Convenience method - determines short/long press and processes.
 * 
 * @param isLongPress true if long press
 */
void handleButtonPress(bool isLongPress = false);

/**
 * @brief Update function (call from main loop)
 * 
 * Handles timeouts, monitors conditions.
 */
void update();

/**
 * @brief Get current status
 */
Status getStatus();

/**
 * @brief Set error condition
 * 
 * @param error Error code
 */
void setError(ErrorCode error);

/**
 * @brief Clear error condition
 */
void clearError();

/**
 * @brief Get last error code
 */
ErrorCode getLastError();

/**
 * @brief Get error description
 */
const char* getErrorString(ErrorCode error);

/**
 * @brief Check if in error state
 */
bool isError();

/**
 * @brief Check if logging is active
 */
bool isLogging();

/**
 * @brief Check if admin mode is active
 */
bool isAdminMode();

/**
 * @brief Force state change (use with caution)
 * 
 * @param newState New state
 */
void forceState(State newState);

// ============================================================================
// Callbacks
// ============================================================================

/** @brief State change callback type */
typedef void (*StateChangeCallback)(State oldState, State newState);

/**
 * @brief Register state change callback
 * 
 * @param callback Function to call on state change
 */
void setStateChangeCallback(StateChangeCallback callback);

} // namespace StateMachine

#endif // STATE_MACHINE_H

