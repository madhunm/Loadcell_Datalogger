/**
 * @file app_mode.h
 * @brief Application Mode Manager for Loadcell Data Logger
 * 
 * Manages three operational modes:
 *   - User:       Normal operation, dashboard view (default on boot)
 *   - FieldAdmin: Calibration and sensor configuration
 *   - Factory:    End-of-line testing after assembly
 * 
 * Mode switching is password-protected for Factory and FieldAdmin modes.
 * Device always boots into User mode (no persistence).
 */

#ifndef APP_MODE_H
#define APP_MODE_H

#include <Arduino.h>

namespace AppMode {

/**
 * @brief Application operational modes
 */
enum class Mode : uint8_t {
    User,       ///< Normal operation - default on boot
    FieldAdmin, ///< Calibration and configuration
    Factory     ///< End-of-line testing
};

/**
 * @brief Initialize the mode manager
 * 
 * Always starts in User mode.
 */
void init();

/**
 * @brief Get the current operational mode
 * 
 * @return Current Mode enum value
 */
Mode getMode();

/**
 * @brief Get the current mode as a string
 * 
 * @return Mode name string ("user", "admin", or "factory")
 */
const char* getModeString();

/**
 * @brief Attempt to switch to a new mode
 * 
 * User mode requires no password.
 * FieldAdmin and Factory modes require correct password.
 * 
 * @param newMode Target mode to switch to
 * @param password Password string (can be nullptr for User mode)
 * @return true if mode switch successful
 */
bool setMode(Mode newMode, const char* password = nullptr);

/**
 * @brief Check if a password is valid for a mode
 * 
 * @param mode Mode to check password for
 * @param password Password to validate
 * @return true if password is correct
 */
bool validatePassword(Mode mode, const char* password);

/**
 * @brief Check if current mode allows logging
 * 
 * Logging is allowed in User and FieldAdmin modes, not in Factory mode.
 * 
 * @return true if logging is permitted
 */
bool canLog();

/**
 * @brief Check if current mode allows sensor configuration
 * 
 * Configuration is allowed in FieldAdmin and Factory modes.
 * 
 * @return true if configuration is permitted
 */
bool canConfigure();

/**
 * @brief Check if current mode allows factory testing
 * 
 * Factory testing is only allowed in Factory mode.
 * 
 * @return true if factory tests are permitted
 */
bool canFactoryTest();

} // namespace AppMode

#endif // APP_MODE_H

