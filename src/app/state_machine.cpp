/**
 * @file state_machine.cpp
 * @brief System State Machine Implementation
 */

#include "state_machine.h"
#include "../drivers/status_led.h"
#include "../drivers/sd_manager.h"
#include "../network/wifi_ap.h"
#include "../network/admin_webui.h"
#include "../logging/logger_module.h"
#include "../logging/bin_to_csv.h"
#include <esp_log.h>

static const char* TAG = "StateMachine";

namespace StateMachine {

namespace {
    // State
    State currentState = State::Init;
    State previousState = State::Init;
    uint32_t stateEntryMs = 0;
    
    // Error tracking
    ErrorCode lastError = ErrorCode::None;
    uint32_t errorCount = 0;
    
    // Callback
    StateChangeCallback stateCallback = nullptr;
    
    // Timeouts (ms)
    constexpr uint32_t PRELOG_TIMEOUT = 5000;
    constexpr uint32_t STOPPING_TIMEOUT = 10000;
    constexpr uint32_t CONVERTING_TIMEOUT = 300000;  // 5 minutes
    
    // State name table
    const char* stateNames[] = {
        "Init",
        "Admin",
        "PreLog",
        "Logging",
        "Stopping",
        "Converting",
        "Ready",
        "Error"
    };
    
    // Error name table
    const char* errorNames[] = {
        "None",
        "SD Missing",
        "SD Full",
        "SD Write Error",
        "ADC Error",
        "IMU Error",
        "RTC Error",
        "Calibration Missing",
        "Buffer Overflow",
        "Critical Error"
    };
    
    // Transition to new state
    void transitionTo(State newState) {
        if (newState == currentState) return;
        
        State oldState = currentState;
        previousState = currentState;
        currentState = newState;
        stateEntryMs = millis();
        
        ESP_LOGI(TAG, "State: %s -> %s", getStateName(oldState), getStateName(newState));
        
        // Update LED based on new state
        switch (newState) {
            case State::Init:
                StatusLED::setState(StatusLED::State::Init);
                break;
            case State::Admin:
                StatusLED::setState(StatusLED::State::IdleAdmin);
                break;
            case State::PreLog:
                // PreLog uses fast blink to indicate preparation
                StatusLED::setCustom(StatusLED::Colors::Orange, StatusLED::Pattern::FastBlink);
                break;
            case State::Logging:
                StatusLED::setState(StatusLED::State::Logging);
                break;
            case State::Stopping:
                StatusLED::setState(StatusLED::State::Stopping);
                break;
            case State::Converting:
                StatusLED::setState(StatusLED::State::Converting);
                break;
            case State::Ready:
                StatusLED::setState(StatusLED::State::Ready);
                break;
            case State::Error:
                // LED will be set based on specific error
                break;
        }
        
        // Notify callback
        if (stateCallback) {
            stateCallback(oldState, newState);
        }
    }
    
    // Handle state entry actions
    void onStateEntry(State state) {
        switch (state) {
            case State::Admin:
                // Start WiFi AP
                WiFiAP::start();
                break;
                
            case State::PreLog:
                // Stop WiFi
                WiFiAP::stop();
                break;
                
            case State::Logging:
                // Start logger
                if (!Logger::start()) {
                    setError(ErrorCode::SdWriteError);
                }
                break;
                
            case State::Stopping:
                // Stop logger
                Logger::stop();
                break;
                
            case State::Converting: {
                // Start conversion
                const char* binPath = Logger::getCurrentFilePath();
                if (binPath && binPath[0]) {
                    BinToCSV::startAsync(binPath);
                }
                break;
            }
            
            case State::Ready:
                // Ensure SD is synced
                SDManager::sync();
                break;
                
            default:
                break;
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void init() {
    currentState = State::Init;
    previousState = State::Init;
    stateEntryMs = millis();
    lastError = ErrorCode::None;
    errorCount = 0;
    
    ESP_LOGI(TAG, "Initialized");
}

State getState() {
    return currentState;
}

const char* getStateName() {
    return getStateName(currentState);
}

const char* getStateName(State state) {
    int idx = static_cast<int>(state);
    if (idx >= 0 && idx < (int)(sizeof(stateNames) / sizeof(stateNames[0]))) {
        return stateNames[idx];
    }
    return "Unknown";
}

void processEvent(Event event) {
    ESP_LOGD(TAG, "Event: %d in state %s", (int)event, getStateName());
    
    State newState = currentState;
    
    switch (currentState) {
        case State::Init:
            if (event == Event::InitComplete) {
                newState = State::Admin;
            } else if (event == Event::Error) {
                newState = State::Error;
            }
            break;
            
        case State::Admin:
            if (event == Event::ButtonShort) {
                // Check SD card
                if (!SDManager::isMounted()) {
                    setError(ErrorCode::SdMissing);
                    newState = State::Error;
                } else {
                    newState = State::PreLog;
                }
            } else if (event == Event::Error) {
                newState = State::Error;
            }
            break;
            
        case State::PreLog:
            if (event == Event::Timeout) {
                newState = State::Logging;
            } else if (event == Event::ButtonShort) {
                // Cancel - return to admin
                newState = State::Admin;
            } else if (event == Event::Error) {
                newState = State::Error;
            }
            break;
            
        case State::Logging:
            if (event == Event::ButtonShort) {
                newState = State::Stopping;
            } else if (event == Event::Error) {
                newState = State::Stopping;  // Try to save what we have
            } else if (event == Event::SdRemoved) {
                setError(ErrorCode::SdMissing);
                newState = State::Error;
            }
            break;
            
        case State::Stopping:
            if (event == Event::LogStopped || event == Event::Timeout) {
                newState = State::Converting;
            } else if (event == Event::Error) {
                newState = State::Error;
            }
            break;
            
        case State::Converting:
            if (event == Event::ConvertComplete || event == Event::Timeout) {
                newState = State::Ready;
            } else if (event == Event::ButtonShort) {
                BinToCSV::cancel();
                newState = State::Ready;
            } else if (event == Event::Error) {
                newState = State::Ready;  // Still allow SD removal
            }
            break;
            
        case State::Ready:
            if (event == Event::ButtonShort || event == Event::AdminMode) {
                newState = State::Admin;
            } else if (event == Event::SdRemoved) {
                // Stay in Ready, just note it
                ESP_LOGI(TAG, "SD card removed");
            }
            break;
            
        case State::Error:
            if (event == Event::ButtonShort || event == Event::ErrorCleared) {
                clearError();
                newState = State::Admin;
            } else if (event == Event::ButtonLong) {
                // Force reset
                clearError();
                newState = State::Admin;
            }
            break;
    }
    
    if (newState != currentState) {
        transitionTo(newState);
        onStateEntry(newState);
    }
}

void handleButtonPress(bool isLongPress) {
    processEvent(isLongPress ? Event::ButtonLong : Event::ButtonShort);
}

void update() {
    uint32_t elapsed = millis() - stateEntryMs;
    
    switch (currentState) {
        case State::PreLog:
            if (elapsed > PRELOG_TIMEOUT) {
                processEvent(Event::Timeout);
            }
            break;
            
        case State::Stopping:
            if (elapsed > STOPPING_TIMEOUT) {
                processEvent(Event::Timeout);
            } else if (!Logger::isRunning()) {
                processEvent(Event::LogStopped);
            }
            break;
            
        case State::Converting:
            if (elapsed > CONVERTING_TIMEOUT) {
                BinToCSV::cancel();
                processEvent(Event::Timeout);
            } else if (!BinToCSV::isRunning()) {
                processEvent(Event::ConvertComplete);
            }
            break;
            
        case State::Logging: {
            // Check for buffer overflow
            Logger::Status logStatus = Logger::getStatus();
            if (logStatus.droppedSamples > 0 || logStatus.droppedBuffers > 0) {
                ESP_LOGW(TAG, "Dropped: %lu samples, %lu buffers",
                         logStatus.droppedSamples, logStatus.droppedBuffers);
            }
            
            // Update logger
            Logger::update();
            break;
        }
            
        default:
            break;
    }
    
    // Check SD card presence
    static bool lastSdPresent = false;
    bool sdPresent = SDManager::isCardPresent();
    
    if (sdPresent != lastSdPresent) {
        lastSdPresent = sdPresent;
        processEvent(sdPresent ? Event::SdInserted : Event::SdRemoved);
    }
}

Status getStatus() {
    Status status;
    status.state = currentState;
    status.previousState = previousState;
    status.stateEntryMs = stateEntryMs;
    status.stateDurationMs = millis() - stateEntryMs;
    status.lastError = lastError;
    status.errorCount = errorCount;
    status.sdCardPresent = SDManager::isCardPresent();
    status.wifiActive = WiFiAP::isReady();
    return status;
}

void setError(ErrorCode error) {
    lastError = error;
    errorCount++;
    
    ESP_LOGE(TAG, "Error: %s", getErrorString(error));
    
    // Set appropriate LED state
    switch (error) {
        case ErrorCode::SdMissing:
            StatusLED::setState(StatusLED::State::ErrSdMissing);
            break;
        case ErrorCode::SdFull:
            StatusLED::setState(StatusLED::State::ErrSdFull);
            break;
        case ErrorCode::SdWriteError:
            StatusLED::setState(StatusLED::State::ErrSdWrite);
            break;
        case ErrorCode::AdcError:
            StatusLED::setState(StatusLED::State::ErrAdc);
            break;
        case ErrorCode::ImuError:
            StatusLED::setState(StatusLED::State::ErrImu);
            break;
        case ErrorCode::RtcError:
            StatusLED::setState(StatusLED::State::ErrRtc);
            break;
        case ErrorCode::CalibrationMissing:
            StatusLED::setState(StatusLED::State::ErrCalibration);
            break;
        case ErrorCode::Critical:
        default:
            StatusLED::setState(StatusLED::State::ErrCritical);
            break;
    }
    
    processEvent(Event::Error);
}

void clearError() {
    lastError = ErrorCode::None;
    processEvent(Event::ErrorCleared);
}

ErrorCode getLastError() {
    return lastError;
}

const char* getErrorString(ErrorCode error) {
    int idx = static_cast<int>(error);
    if (idx >= 0 && idx < (int)(sizeof(errorNames) / sizeof(errorNames[0]))) {
        return errorNames[idx];
    }
    return "Unknown Error";
}

bool isError() {
    return currentState == State::Error;
}

bool isLogging() {
    return currentState == State::Logging;
}

bool isAdminMode() {
    return currentState == State::Admin;
}

void forceState(State newState) {
    ESP_LOGW(TAG, "Force state: %s -> %s", getStateName(), getStateName(newState));
    transitionTo(newState);
    onStateEntry(newState);
}

void setStateChangeCallback(StateChangeCallback callback) {
    stateCallback = callback;
}

} // namespace StateMachine

