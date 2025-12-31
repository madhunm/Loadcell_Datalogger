/**
 * @file calibration_storage.h
 * @brief NVS-based persistent storage for loadcell calibrations
 */

#ifndef CALIBRATION_STORAGE_H
#define CALIBRATION_STORAGE_H

#include "loadcell_types.h"
#include <Preferences.h>

/**
 * @brief Manages persistent storage of loadcell calibration data in NVS
 */
class CalibrationStorage {
public:
    /**
     * @brief Initialize the calibration storage system
     * @return true if successful, false otherwise
     */
    bool begin();
    
    /**
     * @brief Get the number of stored loadcells
     * @return Number of loadcells in storage
     */
    uint8_t getLoadcellCount();
    
    /**
     * @brief Get calibration data for a specific loadcell by index
     * @param index Index of the loadcell (0 to count-1)
     * @param cal Output structure to fill with calibration data
     * @return true if successful, false if index out of range
     */
    bool getLoadcellByIndex(uint8_t index, LoadcellCalibration& cal);
    
    /**
     * @brief Get calibration data for a specific loadcell by ID
     * @param id Loadcell ID string
     * @param cal Output structure to fill with calibration data
     * @return true if found, false otherwise
     */
    bool getLoadcellById(const char* id, LoadcellCalibration& cal);
    
    /**
     * @brief Save or update a loadcell calibration
     * @param cal Calibration data to save
     * @return true if successful, false otherwise
     */
    bool saveLoadcell(const LoadcellCalibration& cal);
    
    /**
     * @brief Delete a loadcell calibration
     * @param id Loadcell ID to delete
     * @return true if successful, false if not found
     */
    bool deleteLoadcell(const char* id);
    
    /**
     * @brief Get the currently active loadcell ID
     * @param id_out Buffer to store the ID (min LOADCELL_ID_LEN bytes)
     * @return true if an active loadcell is set, false otherwise
     */
    bool getActiveLoadcellId(char* id_out);
    
    /**
     * @brief Set the active loadcell by ID
     * @param id Loadcell ID to make active
     * @return true if successful and loadcell exists, false otherwise
     */
    bool setActiveLoadcellId(const char* id);
    
    /**
     * @brief Get the active loadcell calibration data
     * @param cal Output structure to fill
     * @return true if successful, false if no active loadcell set
     */
    bool getActiveLoadcell(LoadcellCalibration& cal);
    
    /**
     * @brief Clear all stored calibrations (factory reset)
     * @return true if successful
     */
    bool clearAll();
    
private:
    Preferences prefs;
    
    /**
     * @brief Generate NVS key for loadcell at given index
     */
    String getLoadcellKey(uint8_t index);
    
    /**
     * @brief Find index of loadcell with given ID
     * @return Index if found, -1 otherwise
     */
    int findLoadcellIndex(const char* id);
};

#endif // CALIBRATION_STORAGE_H
