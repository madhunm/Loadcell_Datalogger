/**
 * @file loadcell_types.h
 * @brief Loadcell Data Type Definitions
 * 
 * Defines structures for loadcell calibration data storage.
 */

#ifndef LOADCELL_TYPES_H
#define LOADCELL_TYPES_H

#include <Arduino.h>

namespace Calibration {

// ============================================================================
// Constants
// ============================================================================

/** @brief Maximum calibration points per loadcell */
constexpr uint8_t MAX_CALIBRATION_POINTS = 16;

/** @brief Maximum ID length */
constexpr size_t MAX_ID_LENGTH = 32;

/** @brief Maximum model name length */
constexpr size_t MAX_MODEL_LENGTH = 16;

/** @brief Maximum serial number length */
constexpr size_t MAX_SERIAL_LENGTH = 16;

// ============================================================================
// Calibration Point
// ============================================================================

/**
 * @brief Single calibration point
 * 
 * Represents one known load/output pair from calibration certificate.
 */
struct CalibrationPoint {
    float load_kg;      // Known load in kilograms
    float output_uV;    // Measured output in microvolts
    
    // Comparison for sorting
    bool operator<(const CalibrationPoint& other) const {
        return output_uV < other.output_uV;
    }
};

// ============================================================================
// Loadcell Calibration
// ============================================================================

/**
 * @brief Complete loadcell calibration data
 * 
 * Contains identification, specifications, and calibration curve.
 */
struct LoadcellCalibration {
    // Identification
    char id[MAX_ID_LENGTH];             // Unique ID: "TC023L0-000025"
    char model[MAX_MODEL_LENGTH];       // Model: "TC023L0"
    char serial[MAX_SERIAL_LENGTH];     // Serial: "000025"
    
    // Specifications
    float capacity_kg;                  // Rated capacity (e.g., 2000.0)
    float excitation_V;                 // Excitation voltage (e.g., 10.0)
    float sensitivity_mVV;              // Rated sensitivity in mV/V
    float zeroBalance_uV;               // Zero balance offset in uV
    
    // Calibration curve
    uint8_t numPoints;                  // Number of valid calibration points
    CalibrationPoint points[MAX_CALIBRATION_POINTS];
    
    // Metadata
    uint32_t calibrationDate;           // Unix timestamp of calibration
    uint32_t lastModified;              // Unix timestamp of last edit
    
    // Initialize to defaults
    void init() {
        memset(id, 0, sizeof(id));
        memset(model, 0, sizeof(model));
        memset(serial, 0, sizeof(serial));
        capacity_kg = 0;
        excitation_V = 10.0f;
        sensitivity_mVV = 2.0f;
        zeroBalance_uV = 0;
        numPoints = 0;
        memset(points, 0, sizeof(points));
        calibrationDate = 0;
        lastModified = 0;
    }
    
    // Generate ID from model and serial
    void generateId() {
        snprintf(id, sizeof(id), "%s-%s", model, serial);
    }
    
    // Check if calibration is valid
    bool isValid() const {
        return id[0] != '\0' && 
               numPoints >= 2 && 
               capacity_kg > 0;
    }
    
    // Add a calibration point
    bool addPoint(float load_kg, float output_uV) {
        if (numPoints >= MAX_CALIBRATION_POINTS) return false;
        points[numPoints].load_kg = load_kg;
        points[numPoints].output_uV = output_uV;
        numPoints++;
        return true;
    }
    
    // Sort calibration points by output voltage (ascending)
    void sortPoints() {
        // Simple bubble sort (small array)
        for (int i = 0; i < numPoints - 1; i++) {
            for (int j = 0; j < numPoints - i - 1; j++) {
                if (points[j].output_uV > points[j + 1].output_uV) {
                    CalibrationPoint temp = points[j];
                    points[j] = points[j + 1];
                    points[j + 1] = temp;
                }
            }
        }
    }
};

// ============================================================================
// Storage Key Generation
// ============================================================================

/**
 * @brief Generate NVS key from loadcell ID
 * 
 * NVS keys are limited to 15 characters, so we hash/truncate the ID.
 */
inline void generateNvsKey(const char* loadcellId, char* keyOut, size_t maxLen) {
    // Simple approach: use first 12 chars + "lc_" prefix
    snprintf(keyOut, maxLen, "lc_%.12s", loadcellId);
    
    // Replace any invalid characters
    for (size_t i = 0; keyOut[i] && i < maxLen; i++) {
        char c = keyOut[i];
        if (!isalnum(c) && c != '_') {
            keyOut[i] = '_';
        }
    }
}

} // namespace Calibration

#endif // LOADCELL_TYPES_H

