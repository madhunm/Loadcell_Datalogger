/**
 * @file calibration_interp.h
 * @brief Interpolation module for converting raw ADC values to load in kg
 */

#ifndef CALIBRATION_INTERP_H
#define CALIBRATION_INTERP_H

#include "loadcell_types.h"

/**
 * @brief Handles conversion from microvolts to kilograms using calibration curves
 */
class CalibrationInterp {
public:
    /**
     * @brief Set the active calibration curve
     * @param cal Calibration data to use for interpolation
     * @return true if calibration is valid, false otherwise
     */
    bool setCalibration(const LoadcellCalibration& cal);
    
    /**
     * @brief Convert microvolts to kilograms using loaded calibration
     * @param uV Measured output in microvolts
     * @return Load in kilograms, or NAN if no calibration loaded
     */
    float convertToKg(float uV);
    
    /**
     * @brief Check if a valid calibration is loaded
     * @return true if ready to convert, false otherwise
     */
    bool isCalibrated() const { return has_calibration; }
    
    /**
     * @brief Get the loaded calibration data
     * @return Pointer to calibration, or nullptr if none loaded
     */
    const LoadcellCalibration* getCalibration() const { 
        return has_calibration ? &active_cal : nullptr; 
    }
    
private:
    LoadcellCalibration active_cal;
    bool has_calibration = false;
    
    /**
     * @brief Validate that calibration curve is usable
     * @return true if valid, false otherwise
     */
    bool validateCalibration(const LoadcellCalibration& cal);
    
    /**
     * @brief Find bracketing points for interpolation
     * @param uV Input value in microvolts
     * @param lower_idx Output: index of lower bracketing point
     * @param upper_idx Output: index of upper bracketing point
     * @return true if brackets found, false if out of range
     */
    bool findBrackets(float uV, uint8_t& lower_idx, uint8_t& upper_idx);
};

#endif // CALIBRATION_INTERP_H
