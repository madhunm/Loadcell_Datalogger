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
#include "../drivers/max17048.h"
#include "../calibration/calibration_storage.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
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
    
    // Sequence counter for gap detection
    std::atomic<uint32_t> adcSequenceNum{0};
    
    // ADC Ring buffer for this logger (128ms at 64ksps for SD latency headroom)
    ADCRingBufferLarge* adcBuffer = nullptr;
    
    // IMU FIFO batch reading
    static constexpr size_t IMU_FIFO_BATCH_SIZE = 32;  // Read up to 32 samples per batch
    LSM6DSV::RawData imuFifoBatch[IMU_FIFO_BATCH_SIZE];
    
    // Logger task handle (pinned to Core 0)
    TaskHandle_t loggerTaskHandle = nullptr;
    volatile bool taskShouldRun = false;
    
    // Write buffer
    uint8_t* writeBuffer = nullptr;
    size_t writeBufferSize = 0;
    size_t writeBufferUsed = 0;
    
    // Estimate file size for pre-allocation
    size_t estimateFileSize(uint32_t adcRateHz, uint32_t imuDecimation, uint32_t durationSec) {
        // ADC: each sample is 16 bytes (timestamp + value + sequence)
        size_t adcBytes = (size_t)adcRateHz * durationSec * sizeof(BinaryFormat::ADCRecord);
        
        // IMU: sampled at adcRateHz / imuDecimation, each record is 28 bytes
        uint32_t imuRate = (imuDecimation > 0) ? (adcRateHz / imuDecimation) : 0;
        size_t imuBytes = (size_t)imuRate * durationSec * sizeof(BinaryFormat::IMURecord);
        
        // Header + footer + 10% margin for events and alignment
        size_t overhead = sizeof(BinaryFormat::FileHeader) + sizeof(BinaryFormat::FileFooter);
        size_t total = adcBytes + imuBytes + overhead;
        
        return (size_t)(total * 1.1);  // 10% safety margin
    }
    
    // Pre-allocate file space to avoid fragmentation
    bool preAllocateFile(File& file, size_t bytes) {
        ESP_LOGI(TAG, "Pre-allocating %.1f MB for log file", bytes / (1024.0 * 1024.0));
        
        // Use fallocate-style approach: seek to end and write a marker
        if (!file.seek(bytes - 1)) {
            ESP_LOGW(TAG, "Pre-allocation seek failed");
            return false;
        }
        
        uint8_t marker = 0;
        if (file.write(&marker, 1) != 1) {
            ESP_LOGW(TAG, "Pre-allocation write failed");
            file.seek(0);
            return false;
        }
        
        // Return to start for actual writing
        file.seek(0);
        ESP_LOGI(TAG, "Pre-allocation successful");
        return true;
    }
    
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
            
            // Write ADC record with sequence number for gap detection
            BinaryFormat::ADCRecord adcRec;
            adcRec.timestampOffsetUs = offsetUs;
            adcRec.rawAdc = sample.raw;
            adcRec.sequenceNum = adcSequenceNum.fetch_add(1, std::memory_order_relaxed);
            
            if (!bufferWrite(&adcRec, sizeof(adcRec))) {
                droppedSamples++;
                continue;
            }
            
            adcSamplesLogged++;
            processed++;
            
        }
        
        // Drain IMU FIFO in batches (more efficient than single reads)
        uint16_t imuSamplesRead = 0;
        if (LSM6DSV::readFIFO(imuFifoBatch, IMU_FIFO_BATCH_SIZE, &imuSamplesRead) && imuSamplesRead > 0) {
            uint32_t nowUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
            
            for (uint16_t i = 0; i < imuSamplesRead; i++) {
                BinaryFormat::IMURecord imuRec;
                imuRec.timestampOffsetUs = nowUs;  // Approximate timestamp
                imuRec.accelX = imuFifoBatch[i].accel[0];
                imuRec.accelY = imuFifoBatch[i].accel[1];
                imuRec.accelZ = imuFifoBatch[i].accel[2];
                imuRec.gyroX = imuFifoBatch[i].gyro[0];
                imuRec.gyroY = imuFifoBatch[i].gyro[1];
                imuRec.gyroZ = imuFifoBatch[i].gyro[2];
                
                if (bufferWrite(&imuRec, sizeof(imuRec))) {
                    imuSamplesLogged++;
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

// Logger task function (runs on Core 0)
void loggerTaskFunc(void* param) {
    ESP_LOGI(TAG, "Logger task started on Core %d", xPortGetCoreID());
    
    // Add task to watchdog (5 second timeout configured in main)
    esp_task_wdt_add(nullptr);
    
    uint32_t lastBatteryCheckMs = 0;
    constexpr uint32_t BATTERY_CHECK_INTERVAL_MS = 10000;  // Every 10 seconds
    constexpr float LOW_BATTERY_THRESHOLD = 5.0f;  // Stop at 5% SOC
    
    while (taskShouldRun) {
        // Feed the watchdog
        esp_task_wdt_reset();
        
        if (running && !paused) {
            processSamples();
            
            // Periodic battery check for low battery protection
            if (millis() - lastBatteryCheckMs > BATTERY_CHECK_INTERVAL_MS) {
                lastBatteryCheckMs = millis();
                
                MAX17048::BatteryData batt;
                if (MAX17048::isPresent() && MAX17048::getBatteryData(&batt)) {
                    if (batt.socPercent < LOW_BATTERY_THRESHOLD) {
                        ESP_LOGW(TAG, "LOW BATTERY (%.1f%%) - stopping logger to protect data",
                                 batt.socPercent);
                        running = false;  // Trigger graceful stop
                        // The stop() function will be called by main loop detecting !running
                    }
                }
            }
        }
        
        // Small delay to prevent tight loop, allows other tasks to run
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Remove from watchdog before exit
    esp_task_wdt_delete(nullptr);
    
    ESP_LOGI(TAG, "Logger task stopping");
    vTaskDelete(nullptr);
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
    
    // Allocate ADC ring buffer (large version for 128ms headroom)
    if (!adcBuffer) {
        adcBuffer = new ADCRingBufferLarge();
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
    
    // Pre-allocate file space to reduce fragmentation-induced write spikes
    if (currentConfig.maxDurationSec > 0) {
        size_t estimatedSize = estimateFileSize(
            currentConfig.adcRateHz,
            currentConfig.imuDecimation,
            currentConfig.maxDurationSec
        );
        if (!preAllocateFile(logFile, estimatedSize)) {
            ESP_LOGW(TAG, "File pre-allocation failed - may have write latency spikes");
        }
    }
    
    // Reset state
    writeBufferUsed = 0;
    adcSamplesLogged = 0;
    imuSamplesLogged = 0;
    bytesWritten = 0;
    droppedSamples = 0;
    droppedBuffers = 0;
    adcSequenceNum = 0;  // Reset sequence counter
    
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
    
    // Configure IMU FIFO for batch reading (zero-loss)
    LSM6DSV::FIFOConfig fifoConfig;
    fifoConfig.watermark = 16;  // Interrupt at 16 samples
    fifoConfig.mode = LSM6DSV::FIFOMode::Continuous;
    fifoConfig.accelBatchRate = LSM6DSV::FIFOBatchRate::Hz120;  // Match logger rate
    fifoConfig.gyroBatchRate = LSM6DSV::FIFOBatchRate::Hz120;
    fifoConfig.enableTimestamp = false;
    
    if (LSM6DSV::configureFIFO(fifoConfig)) {
        LSM6DSV::enableFIFO();
        LSM6DSV::flushFIFO();  // Start fresh
        ESP_LOGI(TAG, "IMU FIFO enabled for batch reads");
    } else {
        ESP_LOGW(TAG, "IMU FIFO config failed, using single reads");
    }
    
    // Start ADC continuous mode
    if (!MAX11270::startContinuous(adcBuffer)) {
        ESP_LOGE(TAG, "Failed to start ADC");
        logFile.close();
        return false;
    }
    
    running = true;
    paused = false;
    
    // Create logger task pinned to Core 0 (ADC ISR runs on Core 1)
    taskShouldRun = true;
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        loggerTaskFunc,             // Task function
        "Logger",                   // Task name
        8192,                       // Stack size (bytes)
        nullptr,                    // Parameters
        configMAX_PRIORITIES - 2,   // High priority (but below ADC ISR)
        &loggerTaskHandle,          // Task handle
        0                           // Core 0 (separate from ADC on Core 1)
    );
    
    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create logger task");
        MAX11270::stopContinuous();
        LSM6DSV::disableFIFO();
        logFile.close();
        running = false;
        return false;
    }
    
    ESP_LOGI(TAG, "Started: %s (task on Core 0)", currentFilePath);
    return true;
}

void stop() {
    if (!running) return;
    
    running = false;
    
    // Stop the logger task
    taskShouldRun = false;
    if (loggerTaskHandle) {
        // Give task time to finish
        vTaskDelay(pdMS_TO_TICKS(50));
        loggerTaskHandle = nullptr;
    }
    
    // Stop ADC
    MAX11270::stopContinuous();
    
    // Disable IMU FIFO
    LSM6DSV::disableFIFO();
    
    // Process any remaining samples
    processSamples();
    
    // Flush write buffer
    flushWriteBuffer();
    
    // Write end marker
    BinaryFormat::EndRecord endRec;
    endRec.type = static_cast<uint8_t>(BinaryFormat::RecordType::End);
    endRec.totalRecords = adcSamplesLogged + imuSamplesLogged;
    endRec.checksum = 0;  // Reserved
    logFile.write((uint8_t*)&endRec, sizeof(endRec));
    
    // Write file footer for integrity verification
    BinaryFormat::FileFooter footer;
    footer.init();
    footer.totalAdcSamples = adcSamplesLogged.load();
    footer.totalImuSamples = imuSamplesLogged.load();
    footer.droppedSamples = droppedSamples.load();
    footer.endTimestampUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
    footer.crc32 = 0;  // TODO: Compute CRC if needed
    logFile.write((uint8_t*)&footer, sizeof(footer));
    
    ESP_LOGI(TAG, "Footer written: %llu ADC, %llu IMU, %lu dropped",
             footer.totalAdcSamples, footer.totalImuSamples, footer.droppedSamples);
    
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

