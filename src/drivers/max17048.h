/**
 * @file max17048.h
 * @brief MAX17048 Fuel Gauge Driver
 * 
 * Features:
 * - I2C communication at 400kHz (address 0x36)
 * - Battery voltage measurement
 * - State of charge (SOC) percentage
 * - Charge/discharge rate monitoring
 * - Low battery alert capability
 */

#ifndef MAX17048_H
#define MAX17048_H

#include <Arduino.h>

namespace MAX17048 {

// ============================================================================
// Device Constants
// ============================================================================

constexpr uint8_t I2C_ADDRESS = 0x36;

// ============================================================================
// Register Definitions
// ============================================================================

namespace Reg {
    constexpr uint8_t VCELL    = 0x02;  // Battery voltage (78.125µV/bit)
    constexpr uint8_t SOC      = 0x04;  // State of charge (1/256% per bit)
    constexpr uint8_t MODE     = 0x06;  // Operating mode
    constexpr uint8_t VERSION  = 0x08;  // IC version
    constexpr uint8_t HIBRT    = 0x0A;  // Hibernate threshold
    constexpr uint8_t CONFIG   = 0x0C;  // Configuration
    constexpr uint8_t VALRT    = 0x14;  // Voltage alert thresholds
    constexpr uint8_t CRATE    = 0x16;  // Charge/discharge rate (%/hr)
    constexpr uint8_t VRESET   = 0x18;  // Voltage reset threshold
    constexpr uint8_t STATUS   = 0x1A;  // Alert status
    constexpr uint8_t CMD      = 0xFE;  // Command register
}

// ============================================================================
// Status Bits
// ============================================================================

namespace StatusBits {
    constexpr uint8_t RI    = 0x01;  // Reset indicator
    constexpr uint8_t VH    = 0x02;  // Voltage high alert
    constexpr uint8_t VL    = 0x04;  // Voltage low alert
    constexpr uint8_t VR    = 0x08;  // Voltage reset alert
    constexpr uint8_t HD    = 0x10;  // SOC 1% change alert
    constexpr uint8_t SC    = 0x20;  // SOC change alert
    constexpr uint8_t ENVR  = 0x40;  // Enable voltage reset alert
}

// ============================================================================
// Data Structures
// ============================================================================

struct BatteryData {
    float voltage;          // Battery voltage in volts
    float socPercent;       // State of charge (0-100%)
    float chargeRate;       // Charge rate in %/hour (negative = discharging)
    bool alertActive;       // True if any alert is active
    uint8_t statusFlags;    // Raw status register
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize the MAX17048 driver
 * @return true if device found and initialized
 * @note Wire must be initialized before calling this
 */
bool init();

/**
 * @brief Check if MAX17048 is present on I2C bus
 * @return true if device responds
 */
bool isPresent();

/**
 * @brief Get the IC version
 * @return Version number (production = 0x0011 or 0x0012)
 */
uint16_t getVersion();

/**
 * @brief Read battery voltage
 * @return Voltage in volts (e.g., 3.85)
 */
float getVoltage();

/**
 * @brief Read state of charge
 * @return SOC percentage (0-100%)
 */
float getSOC();

/**
 * @brief Read charge/discharge rate
 * @return Rate in %/hour (negative = discharging)
 */
float getChargeRate();

/**
 * @brief Read all battery data at once
 * @param data Pointer to BatteryData struct to fill
 * @return true on success
 */
bool getBatteryData(BatteryData* data);

/**
 * @brief Get raw status register
 * @return Status byte
 */
uint8_t getStatus();

/**
 * @brief Clear status alerts
 */
void clearAlerts();

/**
 * @brief Force a quick-start (recalibrate SOC)
 */
void quickStart();

/**
 * @brief Put device into sleep mode
 */
void sleep();

/**
 * @brief Wake device from sleep
 */
void wake();

/**
 * @brief Set voltage alert thresholds
 * @param minV Minimum voltage (triggers alert below this)
 * @param maxV Maximum voltage (triggers alert above this)
 */
void setVoltageAlert(float minV, float maxV);

/**
 * @brief Set low SOC alert threshold
 * @param percent SOC percentage to trigger alert (1-32%)
 */
void setSOCAlert(uint8_t percent);

// ============================================================================
// Low-level Register Access
// ============================================================================

/**
 * @brief Read a 16-bit register
 * @param reg Register address
 * @param value Pointer to store result
 * @return true on success
 */
bool readRegister(uint8_t reg, uint16_t* value);

/**
 * @brief Write a 16-bit register
 * @param reg Register address
 * @param value Value to write
 * @return true on success
 */
bool writeRegister(uint8_t reg, uint16_t value);

} // namespace MAX17048

#endif // MAX17048_H

