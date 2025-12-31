/**
 * @file calibration_storage.cpp
 * @brief NVS-based Loadcell Calibration Storage Implementation
 */

#include "calibration_storage.h"
#include <Preferences.h>
#include <esp_log.h>

static const char* TAG = "CalStorage";

namespace CalibrationStorage {

namespace {
    Preferences prefs;
    bool initialized = false;
    char activeId[Calibration::MAX_ID_LENGTH] = {0};
    
    // Index of stored loadcell IDs
    char storedIds[MAX_LOADCELLS][Calibration::MAX_ID_LENGTH];
    uint8_t storedCount = 0;
    
    // NVS namespace
    constexpr const char* NVS_NAMESPACE = "loadcell";
    constexpr const char* KEY_ACTIVE = "active";
    constexpr const char* KEY_COUNT = "count";
    constexpr const char* KEY_ID_PREFIX = "id_";
    constexpr const char* KEY_CAL_PREFIX = "cal_";
    
    // Save index to NVS
    void saveIndex() {
        prefs.putUChar(KEY_COUNT, storedCount);
        
        for (uint8_t i = 0; i < storedCount; i++) {
            char key[16];
            snprintf(key, sizeof(key), "%s%d", KEY_ID_PREFIX, i);
            prefs.putString(key, storedIds[i]);
        }
    }
    
    // Load index from NVS
    void loadIndex() {
        storedCount = prefs.getUChar(KEY_COUNT, 0);
        if (storedCount > MAX_LOADCELLS) {
            storedCount = MAX_LOADCELLS;
        }
        
        for (uint8_t i = 0; i < storedCount; i++) {
            char key[16];
            snprintf(key, sizeof(key), "%s%d", KEY_ID_PREFIX, i);
            prefs.getString(key, storedIds[i], sizeof(storedIds[i]));
        }
        
        // Load active ID
        prefs.getString(KEY_ACTIVE, activeId, sizeof(activeId));
    }
    
    // Find index of ID in stored list
    int findIdIndex(const char* id) {
        for (uint8_t i = 0; i < storedCount; i++) {
            if (strcmp(storedIds[i], id) == 0) {
                return i;
            }
        }
        return -1;
    }
    
    // Generate calibration key from ID
    void getCalKey(const char* id, char* keyOut, size_t maxLen) {
        // Create a hash-based key to fit NVS 15-char limit
        uint32_t hash = 0;
        for (const char* p = id; *p; p++) {
            hash = hash * 31 + *p;
        }
        snprintf(keyOut, maxLen, "c%08X", hash);
    }
}

// ============================================================================
// Public API
// ============================================================================

bool init() {
    if (initialized) return true;
    
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace");
        return false;
    }
    
    loadIndex();
    
    initialized = true;
    ESP_LOGI(TAG, "Initialized: %d loadcells, active='%s'", storedCount, activeId);
    return true;
}

bool isInitialized() {
    return initialized;
}

bool save(const Calibration::LoadcellCalibration& cal) {
    if (!initialized || !cal.isValid()) {
        ESP_LOGE(TAG, "Invalid calibration or not initialized");
        return false;
    }
    
    // Check if ID already exists
    int idx = findIdIndex(cal.id);
    
    if (idx < 0) {
        // New entry - check capacity
        if (storedCount >= MAX_LOADCELLS) {
            ESP_LOGE(TAG, "Storage full (%d max)", MAX_LOADCELLS);
            return false;
        }
        
        // Add to index
        strncpy(storedIds[storedCount], cal.id, sizeof(storedIds[0]) - 1);
        storedCount++;
        saveIndex();
    }
    
    // Generate key and save calibration blob
    char key[16];
    getCalKey(cal.id, key, sizeof(key));
    
    size_t written = prefs.putBytes(key, &cal, sizeof(cal));
    if (written != sizeof(cal)) {
        ESP_LOGE(TAG, "Failed to save calibration: wrote %zu of %zu", written, sizeof(cal));
        return false;
    }
    
    ESP_LOGI(TAG, "Saved calibration: %s (%d points)", cal.id, cal.numPoints);
    return true;
}

bool load(const char* id, Calibration::LoadcellCalibration* cal) {
    if (!initialized || !id || !cal) return false;
    
    // Check if ID exists
    if (findIdIndex(id) < 0) {
        return false;
    }
    
    // Generate key and load
    char key[16];
    getCalKey(id, key, sizeof(key));
    
    size_t read = prefs.getBytes(key, cal, sizeof(*cal));
    if (read != sizeof(*cal)) {
        ESP_LOGE(TAG, "Failed to load calibration: read %zu of %zu", read, sizeof(*cal));
        return false;
    }
    
    return cal->isValid();
}

bool remove(const char* id) {
    if (!initialized || !id) return false;
    
    int idx = findIdIndex(id);
    if (idx < 0) return false;
    
    // Remove calibration blob
    char key[16];
    getCalKey(id, key, sizeof(key));
    prefs.remove(key);
    
    // Remove from index (shift remaining entries)
    for (int i = idx; i < storedCount - 1; i++) {
        strcpy(storedIds[i], storedIds[i + 1]);
    }
    storedCount--;
    
    // Clear active if it was the removed one
    if (strcmp(activeId, id) == 0) {
        activeId[0] = '\0';
        prefs.remove(KEY_ACTIVE);
    }
    
    saveIndex();
    ESP_LOGI(TAG, "Removed calibration: %s", id);
    return true;
}

bool exists(const char* id) {
    if (!initialized || !id) return false;
    return findIdIndex(id) >= 0;
}

bool setActive(const char* id) {
    if (!initialized) return false;
    
    if (id == nullptr || id[0] == '\0') {
        // Clear active
        activeId[0] = '\0';
        prefs.remove(KEY_ACTIVE);
        ESP_LOGI(TAG, "Cleared active loadcell");
        return true;
    }
    
    // Check if ID exists
    if (findIdIndex(id) < 0) {
        ESP_LOGE(TAG, "Cannot set active: ID not found: %s", id);
        return false;
    }
    
    strncpy(activeId, id, sizeof(activeId) - 1);
    prefs.putString(KEY_ACTIVE, activeId);
    
    ESP_LOGI(TAG, "Set active loadcell: %s", activeId);
    return true;
}

const char* getActiveId() {
    return activeId;
}

bool loadActive(Calibration::LoadcellCalibration* cal) {
    if (!initialized || !cal || activeId[0] == '\0') {
        return false;
    }
    return load(activeId, cal);
}

uint8_t getCount() {
    return storedCount;
}

bool getIdByIndex(uint8_t index, char* idOut, size_t maxLen) {
    if (!initialized || index >= storedCount || !idOut) {
        return false;
    }
    
    strncpy(idOut, storedIds[index], maxLen - 1);
    idOut[maxLen - 1] = '\0';
    return true;
}

void listAll(void (*callback)(const char* id, bool isActive)) {
    if (!initialized || !callback) return;
    
    for (uint8_t i = 0; i < storedCount; i++) {
        bool isActive = (strcmp(storedIds[i], activeId) == 0);
        callback(storedIds[i], isActive);
    }
}

bool clearAll() {
    if (!initialized) return false;
    
    // Remove all calibration blobs
    for (uint8_t i = 0; i < storedCount; i++) {
        char key[16];
        getCalKey(storedIds[i], key, sizeof(key));
        prefs.remove(key);
    }
    
    // Clear index
    storedCount = 0;
    activeId[0] = '\0';
    
    prefs.clear();
    
    ESP_LOGI(TAG, "Cleared all calibrations");
    return true;
}

void commit() {
    // Preferences auto-commits, but we can force it
    prefs.end();
    prefs.begin(NVS_NAMESPACE, false);
}

} // namespace CalibrationStorage

