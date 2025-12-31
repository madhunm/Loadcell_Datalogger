/**
 * @file sd_manager.cpp
 * @brief Implementation of SD card management
 */

#include "sd_manager.h"

void SDManager::updateCardStatus() {
    // Read SD card detect pin (active LOW)
    card_present = (digitalRead(PIN_SD_CD) == SD_CD_ACTIVE_LEVEL);
}

bool SDManager::begin() {
    // Initialize card detect pin
    pinMode(PIN_SD_CD, INPUT_PULLUP);
    
    mounted = false;
    updateCardStatus();
    
    return true;
}

bool SDManager::isCardPresent() {
    updateCardStatus();
    return card_present;
}

bool SDManager::mount() {
    if (mounted) {
        return true;  // Already mounted
    }
    
    if (!isCardPresent()) {
        Serial.println("SD: No card present");
        return false;
    }
    
    // Mount SD card using 1-bit mode (more compatible)
    // Set second parameter to true for 1-bit mode (more compatible)
    // or false for 4-bit mode (faster but requires proper wiring)
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD: Mount failed");
        return false;
    }
    
    mounted = true;
    Serial.println("SD: Mounted successfully");
    
    // Print card info
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SD: No card attached");
        mounted = false;
        return false;
    }
    
    Serial.printf("SD: Type: %s\n", getCardType().c_str());
    Serial.printf("SD: Size: %llu MB\n", getCardSizeMB());
    Serial.printf("SD: Free: %llu MB\n", getFreeSpaceMB());
    
    return true;
}

bool SDManager::unmount() {
    if (!mounted) {
        return true;  // Already unmounted
    }
    
    SD_MMC.end();
    mounted = false;
    Serial.println("SD: Unmounted - safe to remove");
    
    return true;
}

uint64_t SDManager::getCardSizeMB() {
    if (!mounted) return 0;
    return SD_MMC.cardSize() / (1024 * 1024);
}

uint64_t SDManager::getFreeSpaceMB() {
    if (!mounted) return 0;
    uint64_t total = SD_MMC.totalBytes();
    uint64_t used = SD_MMC.usedBytes();
    return (total - used) / (1024 * 1024);
}

uint64_t SDManager::getUsedSpaceMB() {
    if (!mounted) return 0;
    return SD_MMC.usedBytes() / (1024 * 1024);
}

bool SDManager::createDirectory(const char* path) {
    if (!mounted) return false;
    
    // Check if already exists
    if (SD_MMC.exists(path)) {
        return true;
    }
    
    return SD_MMC.mkdir(path);
}

bool SDManager::fileExists(const char* path) {
    if (!mounted) return false;
    return SD_MMC.exists(path);
}

bool SDManager::deleteFile(const char* path) {
    if (!mounted) return false;
    return SD_MMC.remove(path);
}

size_t SDManager::getFileSize(const char* path) {
    if (!mounted || !fileExists(path)) return 0;
    
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) return 0;
    
    size_t size = file.size();
    file.close();
    return size;
}

int SDManager::listFiles(const char* dirPath, void (*callback)(const char* filename, size_t size)) {
    if (!mounted) return 0;
    
    File dir = SD_MMC.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        return 0;
    }
    
    int count = 0;
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            if (callback) {
                callback(file.name(), file.size());
            }
            count++;
        }
        file.close();
        file = dir.openNextFile();
    }
    
    dir.close();
    return count;
}

File SDManager::openWrite(const char* path, bool append) {
    if (!mounted) return File();
    
    const char* mode = append ? FILE_APPEND : FILE_WRITE;
    return SD_MMC.open(path, mode);
}

File SDManager::openRead(const char* path) {
    if (!mounted) return File();
    return SD_MMC.open(path, FILE_READ);
}

void SDManager::flush() {
    // SD_MMC library doesn't have explicit flush, but closing files flushes them
    // This is mainly a placeholder for consistency
}

String SDManager::getCardType() {
    if (!mounted) return "None";
    
    uint8_t cardType = SD_MMC.cardType();
    switch (cardType) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SD";
        case CARD_SDHC: return "SDHC";
        default:        return "Unknown";
    }
}
