/**
 * @file max17048.h
 * @brief MAX17048 Fuel Gauge (Battery Monitor) Driver
 * @details Provides functions to read battery voltage, state of charge (SOC),
 *          and other battery status information from the MAX17048 I2C device.
 * 
 * @author Loadcell Datalogger Project
 * @date December 2024
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

// MAX17048 I2C address (7-bit)
#define MAX17048_I2C_ADDRESS 0x36

// MAX17048 Register Addresses
#define MAX17048_REG_VCELL     0x02  // Cell voltage (16-bit)
#define MAX17048_REG_SOC       0x04  // State of charge (16-bit)
#define MAX17048_REG_MODE      0x06  // Operating mode
#define MAX17048_REG_VERSION   0x08  // IC version
#define MAX17048_REG_HIBRT     0x0A  // Hibernate threshold
#define MAX17048_REG_CONFIG    0x0C  // Configuration
#define MAX17048_REG_VALRT     0x14  // Voltage alert threshold
#define MAX17048_REG_CRATE     0x16  // Charge rate
#define MAX17048_REG_VRESET    0x18  // Voltage reset threshold
#define MAX17048_REG_STATUS    0x1A  // Status register
#define MAX17048_REG_TABLE     0x40  // ModelGauge table start (0x40-0x7F)
#define MAX17048_REG_CMD       0xFE   // Command register

// Battery status structure
struct Max17048Status
{
    float voltage;        // Battery voltage in Volts
    float soc;            // State of charge in percent (0-100)
    float chargeRate;     // Charge/discharge rate in %/hr
    bool alert;           // Alert flag (low voltage, etc.)
    bool powerOnReset;    // Power-on reset flag
};

/**
 * @brief Initialize the MAX17048 fuel gauge
 * @param wire I2C Wire instance (must be initialized)
 * @return true if initialization successful, false otherwise
 */
bool max17048Init(TwoWire &wire);

/**
 * @brief Read battery voltage
 * @return Battery voltage in Volts, or -1.0 on error
 */
float max17048ReadVoltage();

/**
 * @brief Read state of charge (SOC)
 * @return SOC in percent (0-100), or -1.0 on error
 */
float max17048ReadSOC();

/**
 * @brief Read charge/discharge rate
 * @return Charge rate in %/hr (positive = charging, negative = discharging), or 0.0 on error
 */
float max17048ReadChargeRate();

/**
 * @brief Read all battery status information
 * @param status Pointer to Max17048Status structure to fill
 * @return true if read successful, false otherwise
 */
bool max17048ReadStatus(Max17048Status *status);

/**
 * @brief Check if MAX17048 is present and responding
 * @return true if device responds, false otherwise
 */
bool max17048IsPresent();

/**
 * @brief Get IC version
 * @return Version number, or 0 on error
 */
uint16_t max17048GetVersion();


