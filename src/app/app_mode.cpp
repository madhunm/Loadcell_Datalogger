/**
 * @file app_mode.cpp
 * @brief Application Mode Manager Implementation
 */

#include "app_mode.h"
#include <cstring>

namespace AppMode {

// ============================================================================
// Configuration - Passwords (can be overridden via build flags)
// ============================================================================

#ifndef FACTORY_PASSWORD
#define FACTORY_PASSWORD "factory123"
#endif

#ifndef ADMIN_PASSWORD
#define ADMIN_PASSWORD "admin123"
#endif

// ============================================================================
// Private State
// ============================================================================

namespace {
    Mode currentMode = Mode::User;
}

// ============================================================================
// Public API Implementation
// ============================================================================

void init() {
    currentMode = Mode::User;
    Serial.println("[AppMode] Initialized in User mode");
}

Mode getMode() {
    return currentMode;
}

const char* getModeString() {
    switch (currentMode) {
        case Mode::User:       return "user";
        case Mode::FieldAdmin: return "admin";
        case Mode::Factory:    return "factory";
        default:               return "unknown";
    }
}

bool validatePassword(Mode mode, const char* password) {
    if (password == nullptr) {
        return false;
    }
    
    switch (mode) {
        case Mode::User:
            // No password required for User mode
            return true;
            
        case Mode::FieldAdmin:
            return strcmp(password, ADMIN_PASSWORD) == 0;
            
        case Mode::Factory:
            return strcmp(password, FACTORY_PASSWORD) == 0;
            
        default:
            return false;
    }
}

bool setMode(Mode newMode, const char* password) {
    // User mode always allowed
    if (newMode == Mode::User) {
        currentMode = Mode::User;
        Serial.println("[AppMode] Switched to User mode");
        return true;
    }
    
    // Other modes require password
    if (!validatePassword(newMode, password)) {
        Serial.println("[AppMode] Invalid password for mode switch");
        return false;
    }
    
    currentMode = newMode;
    
    const char* modeName;
    switch (newMode) {
        case Mode::FieldAdmin: modeName = "FieldAdmin"; break;
        case Mode::Factory:    modeName = "Factory"; break;
        default:               modeName = "Unknown"; break;
    }
    Serial.println("[AppMode] Switched to " + String(modeName) + " mode");
    
    return true;
}

bool canLog() {
    // Logging allowed in User and FieldAdmin modes
    return currentMode == Mode::User || currentMode == Mode::FieldAdmin;
}

bool canConfigure() {
    // Configuration allowed in FieldAdmin and Factory modes
    return currentMode == Mode::FieldAdmin || currentMode == Mode::Factory;
}

bool canFactoryTest() {
    // Factory testing only in Factory mode
    return currentMode == Mode::Factory;
}

} // namespace AppMode

