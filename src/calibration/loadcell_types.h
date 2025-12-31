/**
 * @file loadcell_types.h
 * @brief Data structures for loadcell calibration management
 */

#ifndef LOADCELL_TYPES_H
#define LOADCELL_TYPES_H

#include <Arduino.h>

/** @brief Maximum number of calibration points per loadcell */
#define MAX_CALIBRATION_POINTS 16

/** @brief Maximum loadcell ID length */
#define LOADCELL_ID_LEN 32

/** @brief Maximum model string length */
#define LOADCELL_MODEL_LEN 16

/** @brief Maximum serial number length */
#define LOADCELL_SERIAL_LEN 16

/**
 * @brief Single calibration point mapping known load to measured output
 */
struct CalibrationPoint {
    float load_kg;      ///< Known applied load in kilograms
    float output_uV;    ///< Measured ADC output in microvolts
};

/**
 * @brief Complete calibration data for a single load cell
 */
struct LoadcellCalibration {
    char id[LOADCELL_ID_LEN];           ///< Unique identifier (e.g., "TC023L0-000025")
    char model[LOADCELL_MODEL_LEN];     ///< Model number (e.g., "TC023L0")
    char serial[LOADCELL_SERIAL_LEN];   ///< Serial number (e.g., "000025")
    float capacity_kg;                   ///< Maximum capacity in kg
    float excitation_V;                  ///< Excitation voltage
    float sensitivity_mVV;               ///< Sensitivity in mV/V (optional)
    uint8_t num_points;                  ///< Number of valid calibration points
    CalibrationPoint points[MAX_CALIBRATION_POINTS]; ///< Calibration curve points
    
    /** @brief Default constructor */
    LoadcellCalibration() : 
        capacity_kg(0), 
        excitation_V(0), 
        sensitivity_mVV(0), 
        num_points(0) {
        memset(id, 0, sizeof(id));
        memset(model, 0, sizeof(model));
        memset(serial, 0, sizeof(serial));
        memset(points, 0, sizeof(points));
    }
};

#endif // LOADCELL_TYPES_H
