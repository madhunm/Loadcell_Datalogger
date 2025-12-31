/**
 * @file calibration_storage.h
 * @brief NVS-based Loadcell Calibration Storage
 * 
 * Provides persistent storage for multiple loadcell calibrations
 * using ESP32 Non-Volatile Storage (NVS).
 * 
 * Storage layout:
 *   Namespace: "loadcell"
 *   Keys:
 *     - "active": Currently selected loadcell ID
 *     - "count": Number of stored loadcells
 *     - "lc_<id>": Serialized LoadcellCalibration blob
 */

#ifndef CALIBRATION_STORAGE_H
#define CALIBRATION_STORAGE_H

#include "loadcell_types.h"

namespace CalibrationStorage {

// ============================================================================
// Constants
// ============================================================================

/** @brief Maximum number of stored loadcells */
constexpr uint8_t MAX_LOADCELLS = 8;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize calibration storage
 * 
 * Opens NVS namespace and loads index.
 * 
 * @return true if initialization successful
 */
bool init();

/**
 * @brief Check if storage is initialized
 */
bool isInitialized();

/**
 * @brief Save a loadcell calibration
 * 
 * Creates new entry or updates existing one with same ID.
 * 
 * @param cal Calibration data to save
 * @return true if saved successfully
 */
bool save(const Calibration::LoadcellCalibration& cal);

/**
 * @brief Load a loadcell calibration by ID
 * 
 * @param id Loadcell ID
 * @param cal Output: loaded calibration data
 * @return true if found and loaded
 */
bool load(const char* id, Calibration::LoadcellCalibration* cal);

/**
 * @brief Remove a loadcell calibration
 * 
 * @param id Loadcell ID to remove
 * @return true if removed successfully
 */
bool remove(const char* id);

/**
 * @brief Check if a loadcell ID exists
 * 
 * @param id Loadcell ID
 * @return true if exists
 */
bool exists(const char* id);

/**
 * @brief Set the active loadcell
 * 
 * @param id Loadcell ID to set as active
 * @return true if set successfully (must exist)
 */
bool setActive(const char* id);

/**
 * @brief Get the active loadcell ID
 * 
 * @return Active loadcell ID, or empty string if none
 */
const char* getActiveId();

/**
 * @brief Load the active loadcell calibration
 * 
 * @param cal Output: active calibration data
 * @return true if active exists and loaded
 */
bool loadActive(Calibration::LoadcellCalibration* cal);

/**
 * @brief Get number of stored loadcells
 */
uint8_t getCount();

/**
 * @brief Get loadcell ID by index
 * 
 * For iterating through stored loadcells.
 * 
 * @param index Index (0 to getCount()-1)
 * @param idOut Output buffer for ID
 * @param maxLen Maximum output length
 * @return true if index valid
 */
bool getIdByIndex(uint8_t index, char* idOut, size_t maxLen);

/**
 * @brief List all stored loadcell IDs
 * 
 * @param callback Function called for each ID
 */
void listAll(void (*callback)(const char* id, bool isActive));

/**
 * @brief Clear all stored calibrations
 * 
 * @return true if cleared successfully
 */
bool clearAll();

/**
 * @brief Commit any pending changes to flash
 */
void commit();

} // namespace CalibrationStorage

#endif // CALIBRATION_STORAGE_H

