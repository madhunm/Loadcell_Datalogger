/**
 * @file calibration_storage.cpp
 * @brief Implementation of NVS-based loadcell calibration storage
 */

#include "calibration_storage.h"

#define NVS_NAMESPACE "loadcell_cal"
#define KEY_COUNT "lc_count"
#define KEY_ACTIVE "active_id"
#define KEY_PREFIX "lc_"

bool CalibrationStorage::begin() {
    return prefs.begin(NVS_NAMESPACE, false);
}

uint8_t CalibrationStorage::getLoadcellCount() {
    return prefs.getUChar(KEY_COUNT, 0);
}

String CalibrationStorage::getLoadcellKey(uint8_t index) {
    return String(KEY_PREFIX) + String(index);
}

int CalibrationStorage::findLoadcellIndex(const char* id) {
    uint8_t count = getLoadcellCount();
    LoadcellCalibration cal;
    
    for (uint8_t i = 0; i < count; i++) {
        if (getLoadcellByIndex(i, cal)) {
            if (strcmp(cal.id, id) == 0) {
                return i;
            }
        }
    }
    return -1;
}

bool CalibrationStorage::getLoadcellByIndex(uint8_t index, LoadcellCalibration& cal) {
    if (index >= getLoadcellCount()) {
        return false;
    }
    
    String key = getLoadcellKey(index);
    size_t len = prefs.getBytes(key.c_str(), &cal, sizeof(LoadcellCalibration));
    return (len == sizeof(LoadcellCalibration));
}

bool CalibrationStorage::getLoadcellById(const char* id, LoadcellCalibration& cal) {
    int index = findLoadcellIndex(id);
    if (index < 0) {
        return false;
    }
    return getLoadcellByIndex(index, cal);
}

bool CalibrationStorage::saveLoadcell(const LoadcellCalibration& cal) {
    // Check if loadcell already exists
    int existing_index = findLoadcellIndex(cal.id);
    
    if (existing_index >= 0) {
        // Update existing
        String key = getLoadcellKey(existing_index);
        size_t written = prefs.putBytes(key.c_str(), &cal, sizeof(LoadcellCalibration));
        return (written == sizeof(LoadcellCalibration));
    } else {
        // Add new
        uint8_t count = getLoadcellCount();
        String key = getLoadcellKey(count);
        size_t written = prefs.putBytes(key.c_str(), &cal, sizeof(LoadcellCalibration));
        
        if (written == sizeof(LoadcellCalibration)) {
            prefs.putUChar(KEY_COUNT, count + 1);
            
            // If this is the first loadcell, make it active
            if (count == 0) {
                setActiveLoadcellId(cal.id);
            }
            return true;
        }
        return false;
    }
}

bool CalibrationStorage::deleteLoadcell(const char* id) {
    int index = findLoadcellIndex(id);
    if (index < 0) {
        return false;
    }
    
    uint8_t count = getLoadcellCount();
    
    // Shift all loadcells after this one down by one index
    for (uint8_t i = index; i < count - 1; i++) {
        LoadcellCalibration cal;
        if (getLoadcellByIndex(i + 1, cal)) {
            String key = getLoadcellKey(i);
            prefs.putBytes(key.c_str(), &cal, sizeof(LoadcellCalibration));
        }
    }
    
    // Remove the last entry
    String last_key = getLoadcellKey(count - 1);
    prefs.remove(last_key.c_str());
    
    // Update count
    prefs.putUChar(KEY_COUNT, count - 1);
    
    // If we deleted the active loadcell, clear active ID
    char active_id[LOADCELL_ID_LEN];
    if (getActiveLoadcellId(active_id) && strcmp(active_id, id) == 0) {
        prefs.remove(KEY_ACTIVE);
        
        // Set first remaining loadcell as active if any exist
        if (count > 1) {
            LoadcellCalibration first;
            if (getLoadcellByIndex(0, first)) {
                setActiveLoadcellId(first.id);
            }
        }
    }
    
    return true;
}

bool CalibrationStorage::getActiveLoadcellId(char* id_out) {
    String active = prefs.getString(KEY_ACTIVE, "");
    if (active.length() == 0) {
        return false;
    }
    strncpy(id_out, active.c_str(), LOADCELL_ID_LEN - 1);
    id_out[LOADCELL_ID_LEN - 1] = '\0';
    return true;
}

bool CalibrationStorage::setActiveLoadcellId(const char* id) {
    // Verify loadcell exists
    LoadcellCalibration cal;
    if (!getLoadcellById(id, cal)) {
        return false;
    }
    
    prefs.putString(KEY_ACTIVE, id);
    return true;
}

bool CalibrationStorage::getActiveLoadcell(LoadcellCalibration& cal) {
    char active_id[LOADCELL_ID_LEN];
    if (!getActiveLoadcellId(active_id)) {
        return false;
    }
    return getLoadcellById(active_id, cal);
}

bool CalibrationStorage::clearAll() {
    return prefs.clear();
}
