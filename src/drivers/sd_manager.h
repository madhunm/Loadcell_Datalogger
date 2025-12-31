/**
 * @file sd_manager.h
 * @brief SD Card Manager with Double-Buffered Async Writes
 * 
 * Features:
 * - SDMMC 4-bit mode (faster than SPI)
 * - Card detect on GPIO10 (active LOW)
 * - Double-buffered async writes for sustained throughput
 * - Background write task on Core 0
 * - Safe mount/unmount for hot-swap
 */

#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <FS.h>

namespace SDManager {

// ============================================================================
// Card Types
// ============================================================================

enum class CardType : uint8_t {
    None = 0,
    MMC,
    SD,
    SDHC,
    Unknown
};

// ============================================================================
// Data Structures
// ============================================================================

/** @brief SD card information */
struct CardInfo {
    CardType type;
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
    uint8_t numSectors;
    uint32_t sectorSize;
};

/** @brief File operation statistics */
struct Stats {
    uint32_t bytesWritten;
    uint32_t bytesRead;
    uint32_t filesOpened;
    uint32_t writeErrors;
    uint32_t readErrors;
    uint32_t bufferSwaps;
    uint32_t droppedBuffers;
};

/** @brief Write buffer state */
struct WriteBuffer {
    uint8_t* data;              // Buffer data pointer
    size_t capacity;            // Buffer capacity
    volatile size_t used;       // Bytes used in buffer
    volatile bool ready;        // Buffer is ready to write
    volatile bool writing;      // Buffer is being written
};

/** @brief Double buffer configuration */
struct DoubleBufferConfig {
    size_t bufferSize;          // Size of each buffer (default 8KB)
    bool usesPSRAM;             // Allocate in PSRAM if available
};

/** @brief Double buffer status */
struct DoubleBufferStatus {
    bool initialized;
    size_t bufferSize;
    size_t activeBufferUsed;
    bool writeInProgress;
    uint32_t pendingBytes;
    uint32_t droppedBuffers;
};

// ============================================================================
// Public API - Basic
// ============================================================================

/**
 * @brief Initialize the SD card manager
 * 
 * Configures the SDMMC peripheral and card detect pin.
 * Does NOT mount the filesystem - call mount() for that.
 * 
 * @return true if initialization successful
 */
bool init();

/**
 * @brief Mount the SD card filesystem
 * 
 * @param formatIfFailed If true, format card if mount fails
 * @return true if mount successful
 */
bool mount(bool formatIfFailed = false);

/**
 * @brief Unmount the SD card filesystem
 * 
 * Ensures all data is flushed and card is safe to remove.
 */
void unmount();

/**
 * @brief Check if filesystem is mounted
 */
bool isMounted();

/**
 * @brief Check if card is physically present
 */
bool isCardPresent();

/**
 * @brief Get card information
 */
bool getCardInfo(CardInfo* info);

/**
 * @brief Get card type as string
 */
const char* getCardTypeString();

/**
 * @brief Get total card capacity in bytes
 */
uint64_t getTotalBytes();

/**
 * @brief Get used space in bytes
 */
uint64_t getUsedBytes();

/**
 * @brief Get free space in bytes
 */
uint64_t getFreeBytes();

// ============================================================================
// Public API - File Operations
// ============================================================================

/**
 * @brief Open a file
 * 
 * @param path File path (must start with /)
 * @param mode Mode string: "r", "w", "a", "r+", "w+", "a+"
 * @return File object (check with operator bool)
 */
File open(const char* path, const char* mode = "r");

/**
 * @brief Check if file or directory exists
 */
bool exists(const char* path);

/**
 * @brief Delete a file
 */
bool remove(const char* path);

/**
 * @brief Rename/move a file
 */
bool rename(const char* pathFrom, const char* pathTo);

/**
 * @brief Create a directory
 */
bool mkdir(const char* path);

/**
 * @brief Remove a directory (must be empty)
 */
bool rmdir(const char* path);

/**
 * @brief List directory contents
 */
File openDir(const char* path);

/**
 * @brief Get operation statistics
 */
Stats getStats();

/**
 * @brief Reset statistics counters
 */
void resetStats();

/**
 * @brief Force sync all pending writes
 */
void sync();

// ============================================================================
// Public API - Double Buffering
// ============================================================================

/**
 * @brief Initialize double buffer system
 * 
 * Allocates two buffers in PSRAM (if available) and starts
 * the background write task on Core 0.
 * 
 * @param config Buffer configuration
 * @return true if initialization successful
 */
bool initDoubleBuffer(const DoubleBufferConfig& config);

/**
 * @brief Initialize double buffer with default settings (8KB buffers)
 */
bool initDoubleBuffer(size_t bufferSize = 8192);

/**
 * @brief Free double buffer resources
 * 
 * Flushes any pending data and frees buffers.
 */
void freeDoubleBuffer();

/**
 * @brief Check if double buffering is enabled
 */
bool isDoubleBufferEnabled();

/**
 * @brief Get double buffer status
 */
DoubleBufferStatus getDoubleBufferStatus();

/**
 * @brief Open file for double-buffered writes
 * 
 * The file will be used by the background write task.
 * Only one file can be active for buffered writes at a time.
 * 
 * @param path File path
 * @return true if file opened successfully
 */
bool openBufferedWrite(const char* path);

/**
 * @brief Close buffered write file
 * 
 * Flushes remaining data and closes the file.
 */
void closeBufferedWrite();

/**
 * @brief Check if buffered write file is open
 */
bool isBufferedWriteOpen();

/**
 * @brief Write data to double buffer (non-blocking)
 * 
 * Copies data to the active buffer. When the buffer is full,
 * it's swapped with the other buffer and written to SD in background.
 * 
 * @param data Data to write
 * @param len Number of bytes
 * @return Number of bytes actually written (may be less if buffer full and not ready to swap)
 */
size_t writeBuffered(const uint8_t* data, size_t len);

/**
 * @brief Get pointer to active buffer for zero-copy writes
 * 
 * Returns pointer to free space in the active buffer.
 * Call commitBufferedWrite() after writing data.
 * 
 * @param availableSpace Output: bytes available in buffer
 * @return Pointer to free space, or nullptr if not available
 */
uint8_t* getWritePointer(size_t* availableSpace);

/**
 * @brief Commit bytes written via getWritePointer()
 * 
 * @param bytesWritten Number of bytes written to the buffer
 * @return true if successful
 */
bool commitBufferedWrite(size_t bytesWritten);

/**
 * @brief Submit current buffer for writing
 * 
 * Forces a buffer swap even if not full. Use when ending a write session.
 */
void submitBuffer();

/**
 * @brief Flush all pending buffered data
 * 
 * Blocks until all buffered data is written to SD.
 * 
 * @param timeoutMs Maximum time to wait
 * @return true if all data flushed, false if timeout
 */
bool flushBufferedWrites(uint32_t timeoutMs = 5000);

/**
 * @brief Check if there are pending writes
 */
bool isWritePending();

/**
 * @brief Get number of dropped buffers (overflow count)
 */
uint32_t getDroppedBufferCount();

} // namespace SDManager

#endif // SD_MANAGER_H
