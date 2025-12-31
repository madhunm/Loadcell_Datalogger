/**
 * @file calibration_interp.h
 * @brief Loadcell Calibration Interpolation
 * 
 * Converts raw ADC values to physical load (kg) using
 * stored calibration curves with linear interpolation.
 */

#ifndef CALIBRATION_INTERP_H
#define CALIBRATION_INTERP_H

#include "loadcell_types.h"

namespace CalibrationInterp {

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Interpolation statistics */
struct Stats {
    uint32_t conversions;       // Total conversions performed
    uint32_t outOfRange;        // Conversions outside calibration range
    uint32_t interpolated;      // Conversions using interpolation
    uint32_t extrapolated;      // Conversions using extrapolation
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize interpolation module
 * 
 * Loads the active calibration from storage.
 * 
 * @return true if initialized with valid calibration
 */
bool init();

/**
 * @brief Check if ready for conversions
 * 
 * @return true if valid calibration is loaded
 */
bool isReady();

/**
 * @brief Reload calibration from storage
 * 
 * Call after changing active loadcell.
 * 
 * @return true if calibration loaded successfully
 */
bool reload();

/**
 * @brief Set calibration directly (without storage)
 * 
 * @param cal Calibration to use
 * @return true if calibration is valid
 */
bool setCalibration(const Calibration::LoadcellCalibration& cal);

/**
 * @brief Get current calibration
 * 
 * @return Pointer to current calibration, or nullptr if none
 */
const Calibration::LoadcellCalibration* getCalibration();

/**
 * @brief Convert raw ADC counts to microvolts
 * 
 * Uses ADC reference voltage and gain settings.
 * Formula: uV = raw * (Vref / 2^bits) * (1e6 / gain)
 * 
 * @param raw Raw 24-bit ADC value
 * @return Voltage in microvolts
 */
float rawToMicrovolts(int32_t raw);

/**
 * @brief Convert microvolts to load (kg)
 * 
 * Uses calibration curve with linear interpolation.
 * 
 * @param uV Voltage in microvolts
 * @return Load in kilograms
 */
float microvoltsToKg(float uV);

/**
 * @brief Convert raw ADC counts directly to load (kg)
 * 
 * Combined conversion: raw -> uV -> kg
 * 
 * @param raw Raw 24-bit ADC value
 * @return Load in kilograms
 */
float rawToKg(int32_t raw);

/**
 * @brief Convert load (kg) to microvolts
 * 
 * Reverse interpolation for calibration verification.
 * 
 * @param kg Load in kilograms
 * @return Expected voltage in microvolts
 */
float kgToMicrovolts(float kg);

/**
 * @brief Get load as percentage of capacity
 * 
 * @param kg Load in kilograms
 * @return Percentage of rated capacity (0-100+)
 */
float getLoadPercent(float kg);

/**
 * @brief Check if load is within calibration range
 * 
 * @param uV Voltage in microvolts
 * @return true if within calibrated range
 */
bool isInRange(float uV);

/**
 * @brief Get minimum calibrated output
 * 
 * @return Minimum output voltage in microvolts
 */
float getMinOutput();

/**
 * @brief Get maximum calibrated output
 * 
 * @return Maximum output voltage in microvolts
 */
float getMaxOutput();

/**
 * @brief Get interpolation statistics
 */
Stats getStats();

/**
 * @brief Reset statistics
 */
void resetStats();

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Set ADC configuration for raw-to-uV conversion
 * 
 * @param vrefMv Reference voltage in millivolts (default 2500)
 * @param bits ADC resolution in bits (default 24)
 * @param gain PGA gain (default 1)
 */
void setADCConfig(float vrefMv, uint8_t bits, uint8_t gain);

/**
 * @brief Enable/disable extrapolation beyond calibration range
 * 
 * When disabled, values outside range are clamped.
 * 
 * @param enable true to enable extrapolation
 */
void setExtrapolationEnabled(bool enable);

} // namespace CalibrationInterp

#endif // CALIBRATION_INTERP_H

