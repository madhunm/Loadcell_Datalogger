/**
 * @file logger_module.cpp
 * @brief High-Rate Data Logger Module Implementation
 */

#include "logger_module.h"
#include "timestamp_sync.h"
#include "binary_format.h"
#include "ring_buffer.h"
#include "../drivers/max11270.h"
#include "../drivers/lsm6dsv.h"
#include "../drivers/sd_manager.h"
#include "../calibration/calibration_storage.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

static const char* TAG = "Logger";

namespace Logger {

namespace {
    // Configuration
    Config currentConfig;
    bool initialized = false;
    
    // State
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    uint64_t sessionStartUs = 0;
    uint32_t sessionStartMs = 0;
    
    // File
    File logFile;
    char currentFilePath[64] = {0};
    char loadcellId[32] = {0};
    
    // Statistics
    std::atomic<uint64_t> adcSamplesLogged{0};
    std::atomic<uint64_t> imuSamplesLogged{0};
    std::atomic<uint32_t> bytesWritten{0};
    std::atomic<uint32_t> droppedSamples{0};
    std::atomic<uint32_t> droppedBuffers{0};
    
    // ADC Ring buffer for this logger
    ADCRingBuffer* adcBuffer = nullptr;
    
    // IMU decimation counter
    uint32_t imuDecimationCounter = 0;
    
    // Write buffer
    uint8_t* writeBuffer = nullptr;
    size_t writeBufferSize = 0;
    size_t writeBufferUsed = 0;
    
    // Generate filename from timestamp
    void generateFilename(char* out, size_t maxLen) {
        uint32_t epoch = TimestampSync::getEpochSeconds();
        if (epoch == 0) {
            epoch = millis() / 1000;  // Fallback
        }
        snprintf(out, maxLen, "%s/log_%lu.bin", currentConfig.outputDir, epoch);
    }
    
    // Write file header
    bool writeHeader() {
        BinaryFormat::FileHeader header;
        header.init();
        
        header.adcSampleRateHz = currentConfig.adcRateHz;
        header.imuSampleRateHz = currentConfig.adcRateHz / currentConfig.imuDecimation;
        header.startTimestampUs = TimestampSync::getEpochMicros();
        
        if (loadcellId[0] != '\0') {
            strncpy(header.loadcellId, loadcellId, sizeof(header.loadcellId) - 1);
        } else {
            const char* activeId = CalibrationStorage::getActiveId();
            if (activeId && activeId[0] != '\0') {
                strncpy(header.loadcellId, activeId, sizeof(header.loadcellId) - 1);
            }
        }
        
        size_t written = logFile.write((uint8_t*)&header, sizeof(header));
        if (written != sizeof(header)) {
            ESP_LOGE(TAG, "Failed to write header");
            return false;
        }
        
        bytesWritten += written;
        return true;
    }
    
    // Flush write buffer to file
    bool flushWriteBuffer() {
        if (writeBufferUsed == 0) return true;
        
        size_t written = logFile.write(writeBuffer, writeBufferUsed);
        if (written != writeBufferUsed) {
            ESP_LOGE(TAG, "Write error: %zu of %zu", written, writeBufferUsed);
            droppedBuffers++;
            return false;
        }
        
        bytesWritten += written;
        writeBufferUsed = 0;
        return true;
    }
    
    // Add data to write buffer
    bool bufferWrite(const void* data, size_t len) {
        if (writeBufferUsed + len > writeBufferSize) {
            if (!flushWriteBuffer()) {
                return false;
            }
        }
        
        memcpy(writeBuffer + writeBufferUsed, data, len);
        writeBufferUsed += len;
        return true;
    }
    
    // Process samples from ring buffer
    void processSamples() {
        if (!running || paused || !adcBuffer) return;
        
        // Process ADC samples from ring buffer
        ADCSample sample;
        uint32_t processed = 0;
        
        while (adcBuffer->pop(sample) && processed < 1000) {
            // Calculate timestamp offset
            uint32_t offsetUs = (uint32_t)(sample.timestamp_us - (uint32_t)sessionStartUs);
            
            // Write ADC record
            BinaryFormat::ADCRecord adcRec;
            adcRec.timestampOffsetUs = offsetUs;
            adcRec.rawAdc = sample.raw;
            
            if (!bufferWrite(&adcRec, sizeof(adcRec))) {
                droppedSamples++;
                continue;
            }
            
            adcSamplesLogged++;
            processed++;
            
            // Check for IMU decimation
            imuDecimationCounter++;
            if (imuDecimationCounter >= currentConfig.imuDecimation) {
                imuDecimationCounter = 0;
                
                // Read IMU data
                LSM6DSV::RawData imuData;
                if (LSM6DSV::readRaw(&imuData)) {
                    BinaryFormat::IMURecord imuRec;
                    imuRec.timestampOffsetUs = offsetUs;
                    imuRec.accelX = imuData.accel[0];
                    imuRec.accelY = imuData.accel[1];
                    imuRec.accelZ = imuData.accel[2];
                    imuRec.gyroX = imuData.gyro[0];
                    imuRec.gyroY = imuData.gyro[1];
                    imuRec.gyroZ = imuData.gyro[2];
                    
                    if (bufferWrite(&imuRec, sizeof(imuRec))) {
                        imuSamplesLogged++;
                    }
                }
            }
        }
        
        // Periodic flush to ensure data is saved
        static uint32_t lastFlushMs = 0;
        if (millis() - lastFlushMs > 1000) {
            flushWriteBuffer();
            logFile.flush();
            lastFlushMs = millis();
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

bool init(const Config& config) {
    if (running) {
        ESP_LOGE(TAG, "Cannot init while running");
        return false;
    }
    
    currentConfig = config;
    
    // Allocate ADC ring buffer
    if (!adcBuffer) {
        adcBuffer = new ADCRingBuffer();
    }
    
    // Allocate write buffer
    writeBufferSize = config.bufferSizeKB * 1024;
    if (writeBuffer) {
        free(writeBuffer);
    }
    writeBuffer = (uint8_t*)ps_malloc(writeBufferSize);
    if (!writeBuffer) {
        writeBuffer = (uint8_t*)malloc(writeBufferSize);
    }
    if (!writeBuffer) {
        ESP_LOGE(TAG, "Failed to allocate write buffer");
        return false;
    }
    
    // Ensure output directory exists
    if (!SDManager::exists(config.outputDir)) {
        if (!SDManager::mkdir(config.outputDir)) {
            ESP_LOGW(TAG, "Failed to create output dir: %s", config.outputDir);
        }
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Initialized: ADC %lu Hz, IMU %lu Hz",
             config.adcRateHz, config.adcRateHz / config.imuDecimation);
    return true;
}

bool init() {
    return init(defaultConfig());
}

bool isInitialized() {
    return initialized;
}

bool start() {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }
    
    // Check SD card
    if (!SDManager::isMounted()) {
        if (!SDManager::mount()) {
            ESP_LOGE(TAG, "SD card not available");
            return false;
        }
    }
    
    // Generate or use filename
    if (currentConfig.autoFilename) {
        generateFilename(currentFilePath, sizeof(currentFilePath));
    } else {
        snprintf(currentFilePath, sizeof(currentFilePath), "%s/%s",
                 currentConfig.outputDir, currentConfig.filename);
    }
    
    // Open file
    logFile = SDManager::open(currentFilePath, FILE_WRITE);
    if (!logFile) {
        ESP_LOGE(TAG, "Failed to open: %s", currentFilePath);
        return false;
    }
    
    // Reset state
    writeBufferUsed = 0;
    adcSamplesLogged = 0;
    imuSamplesLogged = 0;
    bytesWritten = 0;
    droppedSamples = 0;
    droppedBuffers = 0;
    imuDecimationCounter = 0;
    
    // Record start time
    sessionStartUs = TimestampSync::getEpochMicros();
    sessionStartMs = millis();
    
    // Write header
    if (!writeHeader()) {
        logFile.close();
        return false;
    }
    
    // Clear ring buffer
    if (adcBuffer) {
        adcBuffer->reset();
    }
    
    // Start ADC continuous mode
    if (!MAX11270::startContinuous(adcBuffer)) {
        ESP_LOGE(TAG, "Failed to start ADC");
        logFile.close();
        return false;
    }
    
    running = true;
    paused = false;
    
    ESP_LOGI(TAG, "Started: %s", currentFilePath);
    return true;
}

void stop() {
    if (!running) return;
    
    running = false;
    
    // Stop ADC
    MAX11270::stopContinuous();
    
    // Process remaining samples
    processSamples();
    
    // Flush write buffer
    flushWriteBuffer();
    
    // Write end marker
    BinaryFormat::EndRecord endRec;
    endRec.type = static_cast<uint8_t>(BinaryFormat::RecordType::End);
    endRec.totalRecords = adcSamplesLogged + imuSamplesLogged;
    endRec.checksum = 0;  // Reserved
    logFile.write((uint8_t*)&endRec, sizeof(endRec));
    
    // Close file
    logFile.flush();
    logFile.close();
    
    uint32_t durationMs = millis() - sessionStartMs;
    ESP_LOGI(TAG, "Stopped: %llu ADC + %llu IMU samples, %lu bytes, %lu ms",
             adcSamplesLogged.load(), imuSamplesLogged.load(),
             bytesWritten.load(), durationMs);
}

bool isRunning() {
    return running;
}

Status getStatus() {
    Status status;
    status.initialized = initialized;
    status.running = running;
    status.samplesLogged = adcSamplesLogged;
    status.imuSamplesLogged = imuSamplesLogged;
    status.bytesWritten = bytesWritten;
    status.droppedSamples = droppedSamples;
    status.droppedBuffers = droppedBuffers;
    if (adcBuffer) {
        status.fillPercent = (float)adcBuffer->available() / adcBuffer->capacity() * 100.0f;
    } else {
        status.fillPercent = 0;
    }
    status.durationMs = running ? (millis() - sessionStartMs) : 0;
    strncpy(status.currentFile, currentFilePath, sizeof(status.currentFile) - 1);
    return status;
}

const char* getCurrentFilePath() {
    return currentFilePath;
}

void update() {
    if (running && !paused) {
        processSamples();
    }
}

void setLoadcellId(const char* id) {
    if (id) {
        strncpy(loadcellId, id, sizeof(loadcellId) - 1);
    } else {
        loadcellId[0] = '\0';
    }
}

void pause() {
    if (running) {
        paused = true;
        MAX11270::stopContinuous();
        ESP_LOGI(TAG, "Paused");
    }
}

void resume() {
    if (running && paused) {
        paused = false;
        MAX11270::startContinuous(adcBuffer);
        ESP_LOGI(TAG, "Resumed");
    }
}

bool isPaused() {
    return paused;
}

void writeEvent(uint16_t eventCode, const uint8_t* data, size_t dataLen) {
    if (!running) return;
    
    uint8_t eventBuf[sizeof(BinaryFormat::EventRecord) + 256];
    
    BinaryFormat::EventRecord* event = (BinaryFormat::EventRecord*)eventBuf;
    event->timestampOffsetUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
    event->eventCode = eventCode;
    event->dataLength = (uint16_t)dataLen;
    
    if (data && dataLen > 0 && dataLen <= 256) {
        memcpy(eventBuf + sizeof(BinaryFormat::EventRecord), data, dataLen);
    }
    
    // Write event type tag + event record
    uint8_t tag = static_cast<uint8_t>(BinaryFormat::RecordType::Event);
    bufferWrite(&tag, 1);
    bufferWrite(eventBuf, sizeof(BinaryFormat::EventRecord) + dataLen);
}

void getRingBufferStats(size_t* capacity, size_t* used, uint32_t* overflows) {
    if (adcBuffer) {
        if (capacity) *capacity = adcBuffer->capacity();
        if (used) *used = adcBuffer->available();
    } else {
        if (capacity) *capacity = 0;
        if (used) *used = 0;
    }
    if (overflows) {
        MAX11270::Statistics stats = MAX11270::getStatistics();
        *overflows = stats.samplesDropped;
    }
}

bool flush(uint32_t timeoutMs) {
    if (!running) return true;
    
    uint32_t start = millis();
    
    while (millis() - start < timeoutMs) {
        processSamples();
        
        bool bufferEmpty = !adcBuffer || adcBuffer->isEmpty();
        if (bufferEmpty && writeBufferUsed == 0) {
            logFile.flush();
            return true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    return false;
}

} // namespace Logger

