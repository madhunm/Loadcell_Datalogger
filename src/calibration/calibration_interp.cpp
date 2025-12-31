/**
 * @file calibration_interp.cpp
 * @brief Implementation of load cell calibration interpolation
 */

#include "calibration_interp.h"
#include <math.h>

bool CalibrationInterp::validateCalibration(const LoadcellCalibration& cal) {
    // Need at least 2 points for interpolation
    if (cal.num_points < 2) {
        return false;
    }
    
    // Check that points are sorted by output_uV (should be monotonic)
    for (uint8_t i = 0; i < cal.num_points - 1; i++) {
        if (cal.points[i].output_uV >= cal.points[i + 1].output_uV) {
            return false; // Not strictly increasing
        }
    }
    
    return true;
}

bool CalibrationInterp::setCalibration(const LoadcellCalibration& cal) {
    if (!validateCalibration(cal)) {
        has_calibration = false;
        return false;
    }
    
    memcpy(&active_cal, &cal, sizeof(LoadcellCalibration));
    has_calibration = true;
    return true;
}

bool CalibrationInterp::findBrackets(float uV, uint8_t& lower_idx, uint8_t& upper_idx) {
    // Handle out of range cases
    if (uV < active_cal.points[0].output_uV) {
        // Below minimum - extrapolate using first two points
        lower_idx = 0;
        upper_idx = 1;
        return true;
    }
    
    if (uV > active_cal.points[active_cal.num_points - 1].output_uV) {
        // Above maximum - extrapolate using last two points
        lower_idx = active_cal.num_points - 2;
        upper_idx = active_cal.num_points - 1;
        return true;
    }
    
    // Binary search for bracketing points
    for (uint8_t i = 0; i < active_cal.num_points - 1; i++) {
        if (uV >= active_cal.points[i].output_uV && 
            uV <= active_cal.points[i + 1].output_uV) {
            lower_idx = i;
            upper_idx = i + 1;
            return true;
        }
    }
    
    return false;
}

float CalibrationInterp::convertToKg(float uV) {
    if (!has_calibration) {
        return NAN;
    }
    
    uint8_t lower_idx, upper_idx;
    if (!findBrackets(uV, lower_idx, upper_idx)) {
        return NAN;
    }
    
    // Get bracketing points
    const CalibrationPoint& lower = active_cal.points[lower_idx];
    const CalibrationPoint& upper = active_cal.points[upper_idx];
    
    // Linear interpolation
    float uV_range = upper.output_uV - lower.output_uV;
    if (fabs(uV_range) < 0.001f) {
        // Avoid division by zero
        return lower.load_kg;
    }
    
    float ratio = (uV - lower.output_uV) / uV_range;
    float load_kg = lower.load_kg + ratio * (upper.load_kg - lower.load_kg);
    
    // Clamp to valid range (0 to capacity)
    if (load_kg < 0) {
        load_kg = 0;
    } else if (load_kg > active_cal.capacity_kg) {
        load_kg = active_cal.capacity_kg;
    }
    
    return load_kg;
}
