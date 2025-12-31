/**
 * @file sd_manager.cpp
 * @brief SD Card Manager with Double-Buffered Async Writes
 */

#include "sd_manager.h"
#include "../pin_config.h"
#include <SD_MMC.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <cstring>

static const char* TAG = "SDManager";

namespace SDManager {

namespace {
    bool initialized = false;
    bool mounted = false;
    Stats stats = {0};
    
    // ========================================================================
    // Double Buffer State
    // ========================================================================
    
    // Buffer structures
    WriteBuffer bufferA = {nullptr, 0, 0, false, false};
    WriteBuffer bufferB = {nullptr, 0, 0, false, false};
    WriteBuffer* activeBuffer = nullptr;
    WriteBuffer* pendingBuffer = nullptr;
    
    // Double buffer control
    bool doubleBufferEnabled = false;
    size_t configuredBufferSize = 0;
    
    // Background task
    TaskHandle_t writeTaskHandle = nullptr;
    SemaphoreHandle_t bufferMutex = nullptr;
    SemaphoreHandle_t writeReadySema = nullptr;
    volatile bool writeTaskRunning = false;
    
    // Buffered file
    File bufferedFile;
    bool bufferedFileOpen = false;
    
    // Statistics
    std::atomic<uint32_t> droppedBuffers{0};
    std::atomic<uint32_t> bufferSwaps{0};
    
    // ========================================================================
    // Background Write Task
    // ========================================================================
    
    void backgroundWriteTask(void* param) {
        ESP_LOGI(TAG, "Background write task started on Core %d", xPortGetCoreID());
        
        while (writeTaskRunning) {
            // Wait for a buffer to be ready
            if (xSemaphoreTake(writeReadySema, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Find the pending buffer
                WriteBuffer* bufToWrite = nullptr;
                
                if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (pendingBuffer && pendingBuffer->ready && !pendingBuffer->writing) {
                        bufToWrite = pendingBuffer;
                        bufToWrite->writing = true;
                    }
                    xSemaphoreGive(bufferMutex);
                }
                
                // Write the buffer
                if (bufToWrite && bufToWrite->used > 0 && bufferedFileOpen) {
                    size_t bytesToWrite = bufToWrite->used;
                    size_t written = bufferedFile.write(bufToWrite->data, bytesToWrite);
                    
                    if (written == bytesToWrite) {
                        stats.bytesWritten += written;
                    } else {
                        stats.writeErrors++;
                        ESP_LOGE(TAG, "Write error: %zu of %zu bytes", written, bytesToWrite);
                    }
                    
                    // Reset buffer
                    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        bufToWrite->used = 0;
                        bufToWrite->ready = false;
                        bufToWrite->writing = false;
                        pendingBuffer = nullptr;
                        xSemaphoreGive(bufferMutex);
                    }
                }
            }
        }
        
        ESP_LOGI(TAG, "Background write task stopped");
        vTaskDelete(nullptr);
    }
    
    // ========================================================================
    // Buffer Management
    // ========================================================================
    
    bool swapBuffers() {
        if (!doubleBufferEnabled) return false;
        
        // Check if pending buffer is still writing
        if (pendingBuffer && (pendingBuffer->ready || pendingBuffer->writing)) {
            // Can't swap - pending buffer not done
            droppedBuffers++;
            stats.droppedBuffers++;
            return false;
        }
        
        // Swap active and pending
        WriteBuffer* temp = activeBuffer;
        activeBuffer = pendingBuffer ? pendingBuffer : (activeBuffer == &bufferA ? &bufferB : &bufferA);
        
        // Mark old active as pending and ready
        if (temp && temp->used > 0) {
            temp->ready = true;
            pendingBuffer = temp;
            bufferSwaps++;
            stats.bufferSwaps++;
            
            // Signal write task
            xSemaphoreGive(writeReadySema);
        }
        
        return true;
    }
}

// ============================================================================
// Basic Initialization
// ============================================================================

bool init() {
    pinMode(PIN_SD_CD, INPUT_PULLUP);
    
    if (!isCardPresent()) {
        ESP_LOGW(TAG, "No card detected");
        initialized = true;
        return true;
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Initialized");
    return true;
}

bool mount(bool formatIfFailed) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (mounted) return true;
    
    if (!isCardPresent()) {
        ESP_LOGE(TAG, "No card present");
        return false;
    }
    
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    
    // Try 4-bit mode
    if (!SD_MMC.begin("/sdcard", false, formatIfFailed)) {
        ESP_LOGW(TAG, "4-bit mode failed, trying 1-bit");
        if (!SD_MMC.begin("/sdcard", true, formatIfFailed)) {
            ESP_LOGE(TAG, "Mount failed");
            return false;
        }
        ESP_LOGI(TAG, "Mounted in 1-bit mode");
    } else {
        ESP_LOGI(TAG, "Mounted in 4-bit mode");
    }
    
    mounted = true;
    
    CardInfo info;
    if (getCardInfo(&info)) {
        ESP_LOGI(TAG, "Card: %s, Size: %llu MB, Free: %llu MB",
                 getCardTypeString(),
                 info.totalBytes / (1024 * 1024),
                 info.freeBytes / (1024 * 1024));
    }
    
    return true;
}

void unmount() {
    if (!mounted) return;
    
    // Flush buffered writes first
    if (doubleBufferEnabled) {
        flushBufferedWrites(5000);
        closeBufferedWrite();
    }
    
    sync();
    SD_MMC.end();
    mounted = false;
    
    ESP_LOGI(TAG, "Unmounted");
}

bool isMounted() {
    return mounted;
}

bool isCardPresent() {
    return digitalRead(PIN_SD_CD) == SD_CD_ACTIVE_LEVEL;
}

bool getCardInfo(CardInfo* info) {
    if (!info || !mounted) return false;
    
    uint8_t cardType = SD_MMC.cardType();
    
    switch (cardType) {
        case CARD_MMC:  info->type = CardType::MMC; break;
        case CARD_SD:   info->type = CardType::SD; break;
        case CARD_SDHC: info->type = CardType::SDHC; break;
        case CARD_NONE: info->type = CardType::None; break;
        default:        info->type = CardType::Unknown; break;
    }
    
    info->totalBytes = SD_MMC.totalBytes();
    info->usedBytes = SD_MMC.usedBytes();
    info->freeBytes = info->totalBytes - info->usedBytes;
    info->numSectors = SD_MMC.numSectors();
    info->sectorSize = SD_MMC.sectorSize();
    
    return true;
}

const char* getCardTypeString() {
    if (!mounted) return "None";
    
    switch (SD_MMC.cardType()) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SD";
        case CARD_SDHC: return "SDHC";
        default:        return "Unknown";
    }
}

uint64_t getTotalBytes() {
    return mounted ? SD_MMC.totalBytes() : 0;
}

uint64_t getUsedBytes() {
    return mounted ? SD_MMC.usedBytes() : 0;
}

uint64_t getFreeBytes() {
    return mounted ? (SD_MMC.totalBytes() - SD_MMC.usedBytes()) : 0;
}

// ============================================================================
// File Operations
// ============================================================================

File open(const char* path, const char* mode) {
    if (!mounted || !path) return File();
    stats.filesOpened++;
    return SD_MMC.open(path, mode);
}

bool exists(const char* path) {
    return mounted && path && SD_MMC.exists(path);
}

bool remove(const char* path) {
    return mounted && path && SD_MMC.remove(path);
}

bool rename(const char* pathFrom, const char* pathTo) {
    return mounted && pathFrom && pathTo && SD_MMC.rename(pathFrom, pathTo);
}

bool mkdir(const char* path) {
    return mounted && path && SD_MMC.mkdir(path);
}

bool rmdir(const char* path) {
    return mounted && path && SD_MMC.rmdir(path);
}

File openDir(const char* path) {
    return mounted && path ? SD_MMC.open(path) : File();
}

Stats getStats() {
    return stats;
}

void resetStats() {
    memset(&stats, 0, sizeof(stats));
    droppedBuffers = 0;
    bufferSwaps = 0;
}

void sync() {
    // Files need to be flushed individually
}

// ============================================================================
// Double Buffering
// ============================================================================

bool initDoubleBuffer(const DoubleBufferConfig& config) {
    if (doubleBufferEnabled) {
        ESP_LOGW(TAG, "Double buffer already initialized");
        return true;
    }
    
    size_t bufSize = config.bufferSize;
    
    // Allocate buffers (prefer PSRAM)
    if (config.usesPSRAM && psramFound()) {
        bufferA.data = (uint8_t*)ps_malloc(bufSize);
        bufferB.data = (uint8_t*)ps_malloc(bufSize);
        ESP_LOGI(TAG, "Allocated %zu bytes x2 in PSRAM", bufSize);
    } else {
        bufferA.data = (uint8_t*)malloc(bufSize);
        bufferB.data = (uint8_t*)malloc(bufSize);
        ESP_LOGI(TAG, "Allocated %zu bytes x2 in RAM", bufSize);
    }
    
    if (!bufferA.data || !bufferB.data) {
        ESP_LOGE(TAG, "Buffer allocation failed");
        if (bufferA.data) free(bufferA.data);
        if (bufferB.data) free(bufferB.data);
        bufferA.data = nullptr;
        bufferB.data = nullptr;
        return false;
    }
    
    // Initialize buffer structures
    bufferA.capacity = bufSize;
    bufferA.used = 0;
    bufferA.ready = false;
    bufferA.writing = false;
    
    bufferB.capacity = bufSize;
    bufferB.used = 0;
    bufferB.ready = false;
    bufferB.writing = false;
    
    activeBuffer = &bufferA;
    pendingBuffer = nullptr;
    configuredBufferSize = bufSize;
    
    // Create synchronization primitives
    bufferMutex = xSemaphoreCreateMutex();
    writeReadySema = xSemaphoreCreateBinary();
    
    if (!bufferMutex || !writeReadySema) {
        ESP_LOGE(TAG, "Semaphore creation failed");
        freeDoubleBuffer();
        return false;
    }
    
    // Start background write task on Core 0
    writeTaskRunning = true;
    BaseType_t result = xTaskCreatePinnedToCore(
        backgroundWriteTask,
        "sd_writer",
        4096,
        nullptr,
        5,  // Priority
        &writeTaskHandle,
        0   // Core 0
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Write task creation failed");
        freeDoubleBuffer();
        return false;
    }
    
    doubleBufferEnabled = true;
    ESP_LOGI(TAG, "Double buffering enabled (%zu KB x 2)", bufSize / 1024);
    return true;
}

bool initDoubleBuffer(size_t bufferSize) {
    DoubleBufferConfig config = {
        .bufferSize = bufferSize,
        .usesPSRAM = true
    };
    return initDoubleBuffer(config);
}

void freeDoubleBuffer() {
    // Stop write task
    if (writeTaskRunning) {
        writeTaskRunning = false;
        if (writeReadySema) {
            xSemaphoreGive(writeReadySema);  // Wake task to exit
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // Close buffered file
    closeBufferedWrite();
    
    // Free buffers
    if (bufferA.data) {
        free(bufferA.data);
        bufferA.data = nullptr;
    }
    if (bufferB.data) {
        free(bufferB.data);
        bufferB.data = nullptr;
    }
    
    // Delete synchronization primitives
    if (bufferMutex) {
        vSemaphoreDelete(bufferMutex);
        bufferMutex = nullptr;
    }
    if (writeReadySema) {
        vSemaphoreDelete(writeReadySema);
        writeReadySema = nullptr;
    }
    
    activeBuffer = nullptr;
    pendingBuffer = nullptr;
    doubleBufferEnabled = false;
    configuredBufferSize = 0;
    
    ESP_LOGI(TAG, "Double buffering disabled");
}

bool isDoubleBufferEnabled() {
    return doubleBufferEnabled;
}

DoubleBufferStatus getDoubleBufferStatus() {
    DoubleBufferStatus status = {0};
    status.initialized = doubleBufferEnabled;
    status.bufferSize = configuredBufferSize;
    
    if (doubleBufferEnabled && bufferMutex) {
        if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            status.activeBufferUsed = activeBuffer ? activeBuffer->used : 0;
            status.writeInProgress = pendingBuffer && pendingBuffer->writing;
            status.pendingBytes = pendingBuffer ? pendingBuffer->used : 0;
            xSemaphoreGive(bufferMutex);
        }
    }
    
    status.droppedBuffers = droppedBuffers.load();
    return status;
}

bool openBufferedWrite(const char* path) {
    if (!doubleBufferEnabled || !mounted || !path) return false;
    
    if (bufferedFileOpen) {
        closeBufferedWrite();
    }
    
    bufferedFile = SD_MMC.open(path, FILE_WRITE);
    if (!bufferedFile) {
        ESP_LOGE(TAG, "Failed to open buffered file: %s", path);
        return false;
    }
    
    bufferedFileOpen = true;
    stats.filesOpened++;
    ESP_LOGI(TAG, "Opened buffered write: %s", path);
    return true;
}

void closeBufferedWrite() {
    if (!bufferedFileOpen) return;
    
    // Flush remaining data
    flushBufferedWrites(5000);
    
    bufferedFile.close();
    bufferedFileOpen = false;
    ESP_LOGI(TAG, "Closed buffered write");
}

bool isBufferedWriteOpen() {
    return bufferedFileOpen;
}

size_t writeBuffered(const uint8_t* data, size_t len) {
    if (!doubleBufferEnabled || !activeBuffer || !data || len == 0) return 0;
    
    size_t totalWritten = 0;
    
    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    
    while (len > 0) {
        size_t available = activeBuffer->capacity - activeBuffer->used;
        
        if (available == 0) {
            // Buffer full - try to swap
            if (!swapBuffers()) {
                // Can't swap - buffer overflow
                break;
            }
            available = activeBuffer->capacity - activeBuffer->used;
        }
        
        size_t toWrite = (len < available) ? len : available;
        memcpy(activeBuffer->data + activeBuffer->used, data, toWrite);
        activeBuffer->used += toWrite;
        data += toWrite;
        len -= toWrite;
        totalWritten += toWrite;
    }
    
    xSemaphoreGive(bufferMutex);
    return totalWritten;
}

uint8_t* getWritePointer(size_t* availableSpace) {
    if (!doubleBufferEnabled || !activeBuffer || !availableSpace) return nullptr;
    
    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        *availableSpace = 0;
        return nullptr;
    }
    
    *availableSpace = activeBuffer->capacity - activeBuffer->used;
    
    if (*availableSpace == 0) {
        // Try to swap
        if (swapBuffers()) {
            *availableSpace = activeBuffer->capacity - activeBuffer->used;
        }
    }
    
    uint8_t* ptr = (*availableSpace > 0) ? (activeBuffer->data + activeBuffer->used) : nullptr;
    
    xSemaphoreGive(bufferMutex);
    return ptr;
}

bool commitBufferedWrite(size_t bytesWritten) {
    if (!doubleBufferEnabled || !activeBuffer) return false;
    
    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    
    size_t available = activeBuffer->capacity - activeBuffer->used;
    if (bytesWritten > available) {
        xSemaphoreGive(bufferMutex);
        return false;
    }
    
    activeBuffer->used += bytesWritten;
    
    xSemaphoreGive(bufferMutex);
    return true;
}

void submitBuffer() {
    if (!doubleBufferEnabled) return;
    
    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (activeBuffer && activeBuffer->used > 0) {
            swapBuffers();
        }
        xSemaphoreGive(bufferMutex);
    }
}

bool flushBufferedWrites(uint32_t timeoutMs) {
    if (!doubleBufferEnabled) return true;
    
    // Submit current buffer
    submitBuffer();
    
    // Wait for pending write to complete
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        bool pending = false;
        
        if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pending = (pendingBuffer && (pendingBuffer->ready || pendingBuffer->writing));
            xSemaphoreGive(bufferMutex);
        }
        
        if (!pending) {
            // Flush the file
            if (bufferedFileOpen) {
                bufferedFile.flush();
            }
            return true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGW(TAG, "Flush timeout");
    return false;
}

bool isWritePending() {
    if (!doubleBufferEnabled) return false;
    
    bool pending = false;
    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        pending = (pendingBuffer && (pendingBuffer->ready || pendingBuffer->writing));
        pending |= (activeBuffer && activeBuffer->used > 0);
        xSemaphoreGive(bufferMutex);
    }
    return pending;
}

uint32_t getDroppedBufferCount() {
    return droppedBuffers.load();
}

} // namespace SDManager
