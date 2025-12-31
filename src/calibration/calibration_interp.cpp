/**
 * @file calibration_interp.cpp
 * @brief Loadcell Calibration Interpolation Implementation
 */

#include "calibration_interp.h"
#include "calibration_storage.h"
#include <esp_log.h>
#include <cmath>

static const char* TAG = "CalInterp";

namespace CalibrationInterp {

namespace {
    // Current calibration
    Calibration::LoadcellCalibration currentCal;
    bool calibrationLoaded = false;
    
    // ADC configuration
    float adcVrefMv = 2500.0f;      // Reference voltage
    uint8_t adcBits = 24;            // Resolution
    uint8_t adcGain = 1;             // PGA gain
    
    // Options
    bool extrapolationEnabled = true;
    
    // Statistics
    Stats stats = {0};
    
    // Precomputed values
    float adcLsbUv = 0;              // Microvolts per LSB
    float minOutputUv = 0;
    float maxOutputUv = 0;
    
    // Update precomputed values
    void updatePrecomputed() {
        // Calculate LSB size in microvolts
        // Full scale = Vref (differential input)
        // LSB = Vref / 2^(bits-1) for signed
        // In uV: LSB_uV = (Vref_mV * 1000) / 2^(bits-1) / gain
        float fullScale = adcVrefMv * 1000.0f;  // Convert to uV
        float counts = (float)(1UL << (adcBits - 1));
        adcLsbUv = fullScale / counts / adcGain;
        
        // Calculate output range from calibration
        if (calibrationLoaded && currentCal.numPoints >= 2) {
            minOutputUv = currentCal.points[0].output_uV;
            maxOutputUv = currentCal.points[0].output_uV;
            
            for (uint8_t i = 1; i < currentCal.numPoints; i++) {
                if (currentCal.points[i].output_uV < minOutputUv) {
                    minOutputUv = currentCal.points[i].output_uV;
                }
                if (currentCal.points[i].output_uV > maxOutputUv) {
                    maxOutputUv = currentCal.points[i].output_uV;
                }
            }
        }
    }
    
    // Find bracketing points for interpolation
    bool findBracket(float uV, int& lowerIdx, int& upperIdx) {
        if (!calibrationLoaded || currentCal.numPoints < 2) {
            return false;
        }
        
        // Calibration points should be sorted by output_uV
        // Find the two points that bracket the input value
        
        for (uint8_t i = 0; i < currentCal.numPoints - 1; i++) {
            if (uV >= currentCal.points[i].output_uV && 
                uV <= currentCal.points[i + 1].output_uV) {
                lowerIdx = i;
                upperIdx = i + 1;
                return true;
            }
        }
        
        // Not found - check for extrapolation
        if (uV < currentCal.points[0].output_uV) {
            lowerIdx = 0;
            upperIdx = 1;
            return false;  // Below range
        }
        
        lowerIdx = currentCal.numPoints - 2;
        upperIdx = currentCal.numPoints - 1;
        return false;  // Above range
    }
}

// ============================================================================
// Public API
// ============================================================================

bool init() {
    updatePrecomputed();
    
    if (!CalibrationStorage::isInitialized()) {
        if (!CalibrationStorage::init()) {
            ESP_LOGE(TAG, "Failed to init storage");
            return false;
        }
    }
    
    return reload();
}

bool isReady() {
    return calibrationLoaded && currentCal.isValid();
}

bool reload() {
    calibrationLoaded = false;
    
    if (!CalibrationStorage::loadActive(&currentCal)) {
        ESP_LOGW(TAG, "No active calibration");
        return false;
    }
    
    // Ensure points are sorted
    currentCal.sortPoints();
    
    calibrationLoaded = true;
    updatePrecomputed();
    
    ESP_LOGI(TAG, "Loaded calibration: %s (%d points, %.1f kg capacity)",
             currentCal.id, currentCal.numPoints, currentCal.capacity_kg);
    
    return true;
}

bool setCalibration(const Calibration::LoadcellCalibration& cal) {
    if (!cal.isValid()) {
        return false;
    }
    
    currentCal = cal;
    currentCal.sortPoints();
    calibrationLoaded = true;
    updatePrecomputed();
    
    ESP_LOGI(TAG, "Set calibration: %s", currentCal.id);
    return true;
}

const Calibration::LoadcellCalibration* getCalibration() {
    return calibrationLoaded ? &currentCal : nullptr;
}

float rawToMicrovolts(int32_t raw) {
    // Convert 24-bit signed ADC value to microvolts
    return (float)raw * adcLsbUv;
}

float microvoltsToKg(float uV) {
    stats.conversions++;
    
    if (!calibrationLoaded || currentCal.numPoints < 2) {
        // No calibration - use simple linear estimate
        // Assume 2mV/V sensitivity at 10V excitation = 20mV = 20000uV at full scale
        return uV / 20000.0f * 1000.0f;  // Rough estimate
    }
    
    int lowerIdx, upperIdx;
    bool inRange = findBracket(uV, lowerIdx, upperIdx);
    
    if (!inRange) {
        stats.outOfRange++;
        
        if (!extrapolationEnabled) {
            // Clamp to range
            if (uV < minOutputUv) {
                return currentCal.points[0].load_kg;
            } else {
                return currentCal.points[currentCal.numPoints - 1].load_kg;
            }
        }
        stats.extrapolated++;
    } else {
        stats.interpolated++;
    }
    
    // Linear interpolation
    float uV_a = currentCal.points[lowerIdx].output_uV;
    float uV_b = currentCal.points[upperIdx].output_uV;
    float kg_a = currentCal.points[lowerIdx].load_kg;
    float kg_b = currentCal.points[upperIdx].load_kg;
    
    // Handle edge case of identical points
    if (fabsf(uV_b - uV_a) < 0.001f) {
        return (kg_a + kg_b) / 2.0f;
    }
    
    // Interpolate: kg = kg_a + (uV - uV_a) * (kg_b - kg_a) / (uV_b - uV_a)
    float kg = kg_a + (uV - uV_a) * (kg_b - kg_a) / (uV_b - uV_a);
    
    return kg;
}

float rawToKg(int32_t raw) {
    float uV = rawToMicrovolts(raw);
    return microvoltsToKg(uV);
}

float kgToMicrovolts(float kg) {
    if (!calibrationLoaded || currentCal.numPoints < 2) {
        // No calibration - use inverse of simple estimate
        return kg * 20000.0f / 1000.0f;
    }
    
    // Find bracketing points (by load this time)
    int lowerIdx = -1, upperIdx = -1;
    
    for (uint8_t i = 0; i < currentCal.numPoints - 1; i++) {
        float kg_a = currentCal.points[i].load_kg;
        float kg_b = currentCal.points[i + 1].load_kg;
        
        if ((kg >= kg_a && kg <= kg_b) || (kg >= kg_b && kg <= kg_a)) {
            lowerIdx = i;
            upperIdx = i + 1;
            break;
        }
    }
    
    if (lowerIdx < 0) {
        // Extrapolate from first/last two points
        lowerIdx = 0;
        upperIdx = 1;
    }
    
    // Reverse linear interpolation
    float kg_a = currentCal.points[lowerIdx].load_kg;
    float kg_b = currentCal.points[upperIdx].load_kg;
    float uV_a = currentCal.points[lowerIdx].output_uV;
    float uV_b = currentCal.points[upperIdx].output_uV;
    
    if (fabsf(kg_b - kg_a) < 0.001f) {
        return (uV_a + uV_b) / 2.0f;
    }
    
    return uV_a + (kg - kg_a) * (uV_b - uV_a) / (kg_b - kg_a);
}

float getLoadPercent(float kg) {
    if (!calibrationLoaded || currentCal.capacity_kg <= 0) {
        return 0;
    }
    return (kg / currentCal.capacity_kg) * 100.0f;
}

bool isInRange(float uV) {
    return calibrationLoaded && uV >= minOutputUv && uV <= maxOutputUv;
}

float getMinOutput() {
    return minOutputUv;
}

float getMaxOutput() {
    return maxOutputUv;
}

Stats getStats() {
    return stats;
}

void resetStats() {
    memset(&stats, 0, sizeof(stats));
}

void setADCConfig(float vrefMv, uint8_t bits, uint8_t gain) {
    adcVrefMv = vrefMv;
    adcBits = bits;
    adcGain = gain;
    updatePrecomputed();
    
    ESP_LOGI(TAG, "ADC config: Vref=%.1fmV, %d-bit, gain=%d, LSB=%.3fuV",
             vrefMv, bits, gain, adcLsbUv);
}

void setExtrapolationEnabled(bool enable) {
    extrapolationEnabled = enable;
}

} // namespace CalibrationInterp

