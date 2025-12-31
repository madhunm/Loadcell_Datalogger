/**
 * @file sd_manager.h
 * @brief SD card management with SDMMC 4-bit interface
 */

#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <SD_MMC.h>
#include "../pin_config.h"

/**
 * @brief Manages SD card mounting, file operations, and safe unmounting
 */
class SDManager {
public:
    /**
     * @brief Initialize SD card interface
     * @return true if successful, false otherwise
     */
    bool begin();
    
    /**
     * @brief Check if SD card is physically present
     * @return true if card detected, false otherwise
     */
    bool isCardPresent();
    
    /**
     * @brief Check if SD card is mounted and ready
     * @return true if mounted, false otherwise
     */
    bool isMounted() { return mounted; }
    
    /**
     * @brief Mount the SD card filesystem
     * @return true if successful, false otherwise
     */
    bool mount();
    
    /**
     * @brief Unmount the SD card filesystem (safe for removal)
     * @return true if successful
     */
    bool unmount();
    
    /**
     * @brief Get card size in MB
     * @return Size in MB, or 0 if error
     */
    uint64_t getCardSizeMB();
    
    /**
     * @brief Get free space in MB
     * @return Free space in MB, or 0 if error
     */
    uint64_t getFreeSpaceMB();
    
    /**
     * @brief Get used space in MB
     * @return Used space in MB, or 0 if error
     */
    uint64_t getUsedSpaceMB();
    
    /**
     * @brief Create a directory
     * @param path Directory path
     * @return true if successful or already exists
     */
    bool createDirectory(const char* path);
    
    /**
     * @brief Check if a file exists
     * @param path File path
     * @return true if exists, false otherwise
     */
    bool fileExists(const char* path);
    
    /**
     * @brief Delete a file
     * @param path File path
     * @return true if successful
     */
    bool deleteFile(const char* path);
    
    /**
     * @brief Get file size
     * @param path File path
     * @return Size in bytes, or 0 if error/not found
     */
    size_t getFileSize(const char* path);
    
    /**
     * @brief List files in a directory
     * @param dirPath Directory path
     * @param callback Callback function for each file
     * @return Number of files found
     */
    int listFiles(const char* dirPath, void (*callback)(const char* filename, size_t size));
    
    /**
     * @brief Open a file for writing
     * @param path File path
     * @param append true to append, false to overwrite
     * @return File object
     */
    File openWrite(const char* path, bool append = false);
    
    /**
     * @brief Open a file for reading
     * @param path File path
     * @return File object
     */
    File openRead(const char* path);
    
    /**
     * @brief Flush all pending writes to disk
     */
    void flush();
    
    /**
     * @brief Get card type string
     * @return Card type (e.g., "SD", "SDHC", "SDXC")
     */
    String getCardType();
    
private:
    bool mounted;
    bool card_present;
    
    /**
     * @brief Update card presence status
     */
    void updateCardStatus();
};

#endif // SD_MANAGER_H
