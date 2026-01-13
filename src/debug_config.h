/**
 * @file debug_config.h
 * @brief Centralized Debug Configuration
 * 
 * Controls debug output across all modules. Include this header instead of
 * defining DEBUG_VERBOSE locally in each file.
 * 
 * Usage:
 *   #include "debug_config.h"
 *   
 *   #if DEBUG_VERBOSE
 *       Serial.println("Debug info");
 *   #endif
 * 
 * For production builds, set DEBUG_VERBOSE to 0 to disable all debug output.
 * For ESP-IDF logging, use ESP_LOGD/ESP_LOGI/ESP_LOGW/ESP_LOGE macros.
 */

#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

// ============================================================================
// Master Debug Switch
// ============================================================================

/**
 * @brief Enable/disable verbose debug output
 * 
 * Set to 1 for development, 0 for production.
 * This affects Serial.print debug statements wrapped in #if DEBUG_VERBOSE.
 */
#ifndef DEBUG_VERBOSE
#define DEBUG_VERBOSE 0
#endif

#if !DEBUG_VERBOSE
// Null Serial to swallow debug prints when DEBUG_VERBOSE == 0
struct NullSerialType {
    template<typename... Args> void print(Args...) {}
    template<typename... Args> void println(Args...) {}
    template<typename... Args> void printf(const char*, Args...) {}
    template<typename... Args> void printf(Args...) {}
    void begin(...) {}
    operator bool() const { return true; }
    int available() const { return 0; }
};
static NullSerialType NullSerial;
#define Serial NullSerial
#endif

// ============================================================================
// Per-Module Debug Switches (for granular control)
// ============================================================================

/** ADC driver debug output */
#ifndef DEBUG_ADC
#define DEBUG_ADC DEBUG_VERBOSE
#endif

/** IMU driver debug output */
#ifndef DEBUG_IMU
#define DEBUG_IMU DEBUG_VERBOSE
#endif

/** SD card debug output */
#ifndef DEBUG_SD
#define DEBUG_SD DEBUG_VERBOSE
#endif

/** Logger module debug output */
#ifndef DEBUG_LOGGER
#define DEBUG_LOGGER DEBUG_VERBOSE
#endif

/** WebUI debug output */
#ifndef DEBUG_WEBUI
#define DEBUG_WEBUI DEBUG_VERBOSE
#endif

/** Calibration debug output */
#ifndef DEBUG_CAL
#define DEBUG_CAL DEBUG_VERBOSE
#endif

// ============================================================================
// ESP-IDF Log Level Configuration
// ============================================================================

/**
 * ESP-IDF logging uses these levels:
 *   ESP_LOGE - Error (always shown)
 *   ESP_LOGW - Warning
 *   ESP_LOGI - Info
 *   ESP_LOGD - Debug (only with debug build)
 *   ESP_LOGV - Verbose
 * 
 * Set log level in platformio.ini:
 *   build_flags = -DCORE_DEBUG_LEVEL=3  ; Info level
 * 
 * Levels: 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
 */

// ============================================================================
// Debug Helper Macros
// ============================================================================

/**
 * @brief Debug print macro that includes function name
 * 
 * Usage: DBG_PRINT("Value: %d", value);
 */
#if DEBUG_VERBOSE
#define DBG_PRINT(fmt, ...) Serial.printf("[%s] " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define DBG_PRINT(fmt, ...) ((void)0)
#endif

/**
 * @brief Conditional debug print for specific module
 * 
 * Usage: DBG_MODULE(DEBUG_ADC, "ADC value: %d", raw);
 */
#define DBG_MODULE(module, fmt, ...) \
    do { if (module) Serial.printf(fmt "\n", ##__VA_ARGS__); } while(0)

#endif // DEBUG_CONFIG_H
