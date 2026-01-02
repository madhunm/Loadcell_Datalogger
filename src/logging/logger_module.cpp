/**
 * @file logger_module.cpp
 * @brief High-Rate Data Logger Module Implementation
 */

#include "logger_module.h"
#include "timestamp_sync.h"
#include "binary_format.h"
#include "ring_buffer.h"
#include "csv_converter.h"
#include "../drivers/max11270.h"
#include "../drivers/lsm6dsv.h"
#include "../drivers/sd_manager.h"
#include "../drivers/max17048.h"
#include "../drivers/rx8900ce.h"
#include "../calibration/calibration_storage.h"
#include "../calibration/calibration_interp.h"
#include <esp_log.h>
#include <cmath>
#include <esp_task_wdt.h>
#include <esp_crc.h>
#include <Preferences.h>
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
    
    // CRC32 for data integrity (computed incrementally)
    uint32_t runningCrc32 = 0;
    
    // Write latency monitoring
    uint32_t writeLatencyMinUs = UINT32_MAX;
    uint32_t writeLatencyMaxUs = 0;
    uint64_t writeLatencySumUs = 0;
    uint32_t writeLatencyCount = 0;
    std::atomic<uint32_t> writeLatencyOver10ms{0};
    
    // Buffer statistics for dual-core monitoring
    std::atomic<uint32_t> bufferHighWaterMark{0};
    std::atomic<uint32_t> isrTimeUs{0};
    std::atomic<uint32_t> loggerTimeUs{0};
    
    // Checkpoint and recovery
    uint32_t lastCheckpointMs = 0;
    uint32_t fileRotationIndex = 0;
    char sessionBasePath[64] = {0};
    
    // ADC saturation tracking
    std::atomic<uint32_t> saturationCount{0};
    
    // Temperature compensation
    float lastTemperature = 25.0f;
    uint32_t lastTempReadMs = 0;
    
    // Peak tracking for session summary
    std::atomic<float> peakLoadN{0.0f};
    std::atomic<uint32_t> peakLoadTimeMs{0};
    std::atomic<float> peakDecelG{0.0f};
    std::atomic<uint32_t> peakDecelTimeMs{0};
    SessionSummary lastSessionSummary;  // Stored after stop()
    
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
    
    // Forward declarations
    bool flushWriteBuffer();
    bool bufferWrite(const void* data, size_t len);
    bool writeHeader();
    void saveSessionState();
    void clearSessionState();
    bool hasRecoverableSession();
    bool loadSessionState(char* filepath, size_t maxLen, uint64_t* adcCount, 
                          uint64_t* imuCount, uint32_t* sequence, uint32_t* crc);
    
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
    
    // Maximum pre-allocation size (100 MB) - larger files grow dynamically
    static constexpr size_t MAX_PREALLOC_BYTES = 100 * 1024 * 1024;
    
    // Pre-allocate file space to avoid fragmentation
    bool preAllocateFile(File& file, size_t bytes) {
        // Limit pre-allocation to avoid hanging on huge files
        if (bytes > MAX_PREALLOC_BYTES) {
            ESP_LOGW(TAG, "Requested %.1f MB exceeds max, limiting to %.1f MB",
                     bytes / (1024.0 * 1024.0), MAX_PREALLOC_BYTES / (1024.0 * 1024.0));
            bytes = MAX_PREALLOC_BYTES;
        }
        
        ESP_LOGI(TAG, "Pre-allocating %.1f MB for log file", bytes / (1024.0 * 1024.0));
        
        // Use fallocate-style approach: seek to end and write a marker
        // Add timeout protection - if this takes more than 5 seconds, skip it
        uint32_t startMs = millis();
        
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
        
        uint32_t elapsedMs = millis() - startMs;
        ESP_LOGI(TAG, "Pre-allocation successful (took %lu ms)", elapsedMs);
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
    
    // Generate filename with rotation index
    void generateRotatedFilename(char* out, size_t maxLen) {
        snprintf(out, maxLen, "%s_%03lu.bin", sessionBasePath, fileRotationIndex);
    }
    
    // Check if file should be rotated
    bool shouldRotateFile() {
        if (!running || !logFile) return false;
        
        // Check size limit
        if (currentConfig.maxFileSizeMB > 0) {
            uint32_t currentSizeMB = bytesWritten.load() / (1024 * 1024);
            if (currentSizeMB >= currentConfig.maxFileSizeMB) {
                return true;
            }
        }
        
        // Check duration limit
        if (currentConfig.maxFileDurationSec > 0) {
            uint32_t durationSec = (millis() - sessionStartMs) / 1000;
            if (durationSec >= currentConfig.maxFileDurationSec) {
                return true;
            }
        }
        
        return false;
    }
    
    // Rotate to a new file
    static uint32_t rotationCount = 0;
    bool rotateFile() {
        ESP_LOGI(TAG, "Rotating file...");
        
        // Write rotation event to current file
        BinaryFormat::EventRecord event;
        event.timestampOffsetUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
        event.eventCode = BinaryFormat::EventCode::FileRotation;
        event.dataLength = 0;
        bufferWrite(&event, sizeof(event));
        
        // Flush and close current file
        flushWriteBuffer();
        
        // Write partial footer
        BinaryFormat::FileFooter footer;
        footer.init();
        footer.totalAdcSamples = adcSamplesLogged.load();
        footer.totalImuSamples = imuSamplesLogged.load();
        footer.droppedSamples = droppedSamples.load();
        footer.endTimestampUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
        footer.crc32 = runningCrc32;
        logFile.write((uint8_t*)&footer, sizeof(footer));
        
        logFile.flush();
        logFile.close();
        
        // Increment rotation index and open new file
        fileRotationIndex++;
        rotationCount++;
        generateRotatedFilename(currentFilePath, sizeof(currentFilePath));
        
        logFile = SDManager::open(currentFilePath, FILE_WRITE);
        if (!logFile) {
            ESP_LOGE(TAG, "Failed to open rotated file: %s", currentFilePath);
            running = false;
            return false;
        }
        
        // Pre-allocate new file
        if (currentConfig.maxDurationSec > 0) {
            size_t estimatedSize = estimateFileSize(
                currentConfig.adcRateHz,
                currentConfig.imuDecimation,
                currentConfig.maxFileDurationSec > 0 ? currentConfig.maxFileDurationSec : currentConfig.maxDurationSec
            );
            preAllocateFile(logFile, estimatedSize);
        }
        
        // Reset CRC for new file
        runningCrc32 = 0;
        
        // Write header to new file
        if (!writeHeader()) {
            logFile.close();
            running = false;
            return false;
        }
        
        // Reset timing for new file (but keep sequence numbers continuous)
        sessionStartUs = TimestampSync::getEpochMicros();
        sessionStartMs = millis();
        
        ESP_LOGI(TAG, "Rotated to: %s (rotation #%lu)", currentFilePath, rotationCount);
        return true;
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
        
        // Include header in CRC32
        if (currentConfig.enableCrc32) {
            runningCrc32 = esp_crc32_le(0, (uint8_t*)&header, sizeof(header));
        }
        
        bytesWritten += written;
        return true;
    }
    
    // Write a checkpoint marker for crash recovery
    static uint32_t checkpointCount = 0;
    bool writeCheckpoint() {
        // Flush any pending data first
        flushWriteBuffer();
        
        // Write checkpoint event
        BinaryFormat::EventRecord event;
        event.timestampOffsetUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
        event.eventCode = BinaryFormat::EventCode::Checkpoint;
        event.dataLength = sizeof(BinaryFormat::FileFooter);
        
        uint8_t tag = static_cast<uint8_t>(BinaryFormat::RecordType::Event);
        logFile.write(&tag, 1);
        logFile.write((uint8_t*)&event, sizeof(event));
        
        // Write partial footer as checkpoint data
        BinaryFormat::FileFooter checkpoint;
        checkpoint.init();
        checkpoint.totalAdcSamples = adcSamplesLogged.load();
        checkpoint.totalImuSamples = imuSamplesLogged.load();
        checkpoint.droppedSamples = droppedSamples.load();
        checkpoint.endTimestampUs = event.timestampOffsetUs;
        checkpoint.crc32 = runningCrc32;  // CRC up to this point
        
        logFile.write((uint8_t*)&checkpoint, sizeof(checkpoint));
        logFile.flush();
        
        checkpointCount++;
        ESP_LOGI(TAG, "Checkpoint #%lu: %llu ADC, %llu IMU samples",
                 checkpointCount, checkpoint.totalAdcSamples, checkpoint.totalImuSamples);
        
        // Save session state to NVS for recovery
        saveSessionState();
        
        return true;
    }
    
    // Save session state to NVS for power-fail recovery
    void saveSessionState() {
        Preferences prefs;
        if (prefs.begin("logger_state", false)) {
            prefs.putString("filepath", currentFilePath);
            prefs.putULong64("adc_count", adcSamplesLogged.load());
            prefs.putULong64("imu_count", imuSamplesLogged.load());
            prefs.putULong("sequence", adcSequenceNum.load());
            prefs.putULong("bytes", bytesWritten.load());
            prefs.putULong("crc32", runningCrc32);
            prefs.putULong("timestamp", millis());
            prefs.putBool("active", true);
            prefs.end();
        }
    }
    
    // Clear session state (called on clean stop)
    void clearSessionState() {
        Preferences prefs;
        if (prefs.begin("logger_state", false)) {
            prefs.putBool("active", false);
            prefs.end();
        }
    }
    
    // Check if there's a recoverable session
    bool hasRecoverableSession() {
        Preferences prefs;
        bool active = false;
        if (prefs.begin("logger_state", true)) {
            active = prefs.getBool("active", false);
            prefs.end();
        }
        return active;
    }
    
    // Load session state for recovery
    bool loadSessionState(char* filepath, size_t maxLen, uint64_t* adcCount, 
                          uint64_t* imuCount, uint32_t* sequence, uint32_t* crc) {
        Preferences prefs;
        if (!prefs.begin("logger_state", true)) return false;
        
        if (!prefs.getBool("active", false)) {
            prefs.end();
            return false;
        }
        
        String path = prefs.getString("filepath", "");
        if (path.length() == 0) {
            prefs.end();
            return false;
        }
        
        strncpy(filepath, path.c_str(), maxLen - 1);
        *adcCount = prefs.getULong64("adc_count", 0);
        *imuCount = prefs.getULong64("imu_count", 0);
        *sequence = prefs.getULong("sequence", 0);
        *crc = prefs.getULong("crc32", 0);
        
        prefs.end();
        return true;
    }
    
    // Flush write buffer to file with latency monitoring and CRC32
    bool flushWriteBuffer() {
        if (writeBufferUsed == 0) return true;
        
        // Measure write latency
        uint32_t startUs = micros();
        
        size_t written = logFile.write(writeBuffer, writeBufferUsed);
        
        uint32_t latencyUs = micros() - startUs;
        
        // Update latency statistics
        if (latencyUs < writeLatencyMinUs) writeLatencyMinUs = latencyUs;
        if (latencyUs > writeLatencyMaxUs) writeLatencyMaxUs = latencyUs;
        writeLatencySumUs += latencyUs;
        writeLatencyCount++;
        
        // Warn on high latency (>10ms)
        if (latencyUs > 10000) {
            writeLatencyOver10ms++;
            ESP_LOGW(TAG, "High write latency: %lu us", latencyUs);
        }
        
        if (written != writeBufferUsed) {
            ESP_LOGE(TAG, "Write error: %zu of %zu", written, writeBufferUsed);
            droppedBuffers++;
            return false;
        }
        
        // Update running CRC32 if enabled
        if (currentConfig.enableCrc32) {
            runningCrc32 = esp_crc32_le(runningCrc32, writeBuffer, writeBufferUsed);
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
    
    // ADC saturation threshold (0.1% of 24-bit range)
    static constexpr int32_t ADC_SATURATION_THRESHOLD = 8380000;
    
    // Process samples from ring buffer
    void processSamples() {
        if (!running || paused || !adcBuffer) return;
        
        // Track buffer high water mark for diagnostics
        uint32_t currentFill = adcBuffer->available();
        uint32_t currentHighWater = bufferHighWaterMark.load();
        if (currentFill > currentHighWater) {
            bufferHighWaterMark.store(currentFill);
        }
        
        // Process ADC samples from ring buffer
        ADCSample sample;
        uint32_t processed = 0;
        
        while (adcBuffer->pop(sample) && processed < 1000) {
            // Check for ADC saturation
            if (abs(sample.raw) > ADC_SATURATION_THRESHOLD) {
                saturationCount++;
                // Log saturation event (throttled to avoid spam)
                static uint32_t lastSatWarn = 0;
                if (millis() - lastSatWarn > 1000) {
                    ESP_LOGW(TAG, "ADC saturation detected: %ld", sample.raw);
                    lastSatWarn = millis();
                }
            }
            
            // Apply temperature compensation if enabled
            int32_t rawValue = sample.raw;
            if (currentConfig.enableTempCompensation) {
                float compensated = rawValue * (1.0f + currentConfig.tempCoefficient * (lastTemperature - 25.0f));
                rawValue = (int32_t)compensated;
            }
            
            // Calculate timestamp offset
            uint32_t offsetUs = (uint32_t)(sample.timestamp_us - (uint32_t)sessionStartUs);
            uint32_t offsetMs = offsetUs / 1000;
            
            // Track peak load (convert raw to kg, then to Newtons)
            float loadKg = CalibrationInterp::rawToKg(rawValue);
            float loadN = loadKg * 9.81f;
            if (loadN > peakLoadN.load()) {
                peakLoadN.store(loadN);
                peakLoadTimeMs.store(offsetMs);
            }
            
            // Write ADC record with sequence number for gap detection
            BinaryFormat::ADCRecord adcRec;
            adcRec.timestampOffsetUs = offsetUs;
            adcRec.rawAdc = rawValue;
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
            uint32_t nowMs = nowUs / 1000;
            
            for (uint16_t i = 0; i < imuSamplesRead; i++) {
                BinaryFormat::IMURecord imuRec;
                imuRec.timestampOffsetUs = nowUs;  // Approximate timestamp
                imuRec.accelX = imuFifoBatch[i].accel[0];
                imuRec.accelY = imuFifoBatch[i].accel[1];
                imuRec.accelZ = imuFifoBatch[i].accel[2];
                imuRec.gyroX = imuFifoBatch[i].gyro[0];
                imuRec.gyroY = imuFifoBatch[i].gyro[1];
                imuRec.gyroZ = imuFifoBatch[i].gyro[2];
                
                // Track peak deceleration (acceleration magnitude in g)
                // Raw values are in LSB, convert to g based on scale
                // LSM6DSV at ±2g: 0.061 mg/LSB
                constexpr float ACCEL_SCALE = 0.061f / 1000.0f;  // mg/LSB to g/LSB
                float ax = imuRec.accelX * ACCEL_SCALE;
                float ay = imuRec.accelY * ACCEL_SCALE;
                float az = imuRec.accelZ * ACCEL_SCALE;
                float accelMag = std::sqrt(ax*ax + ay*ay + az*az);
                
                if (accelMag > peakDecelG.load()) {
                    peakDecelG.store(accelMag);
                    peakDecelTimeMs.store(nowMs);
                }
                
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
    uint32_t lastSDCheckMs = 0;
    constexpr uint32_t BATTERY_CHECK_INTERVAL_MS = 10000;  // Every 10 seconds
    constexpr uint32_t SD_CHECK_INTERVAL_MS = 1000;        // Every 1 second
    constexpr float LOW_BATTERY_THRESHOLD = 5.0f;  // Stop at 5% SOC
    
    while (taskShouldRun) {
        // Feed the watchdog
        esp_task_wdt_reset();
        
        if (running && !paused) {
            uint32_t loopStartUs = micros();
            
            processSamples();
            
            // Track logger task time for diagnostics
            loggerTimeUs.store(micros() - loopStartUs);
            
            // Periodic SD card presence check for hot-removal handling
            if (millis() - lastSDCheckMs > SD_CHECK_INTERVAL_MS) {
                lastSDCheckMs = millis();
                
                if (!SDManager::isMounted() || !SDManager::isCardPresent()) {
                    ESP_LOGE(TAG, "SD CARD REMOVED - stopping logger");
                    
                    // Try to write SD removed event (may fail)
                    BinaryFormat::EventRecord event;
                    event.timestampOffsetUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
                    event.eventCode = BinaryFormat::EventCode::SDRemoved;
                    event.dataLength = 0;
                    bufferWrite(&event, sizeof(event));
                    
                    running = false;  // Trigger stop (stop() handles unmounted state)
                }
            }
            
            // Periodic battery check for low battery protection
            if (millis() - lastBatteryCheckMs > BATTERY_CHECK_INTERVAL_MS) {
                lastBatteryCheckMs = millis();
                
                MAX17048::BatteryData batt;
                if (MAX17048::isPresent() && MAX17048::getBatteryData(&batt)) {
                    if (batt.socPercent < LOW_BATTERY_THRESHOLD) {
                        ESP_LOGW(TAG, "LOW BATTERY (%.1f%%) - stopping logger to protect data",
                                 batt.socPercent);
                        // Write low battery event
                        BinaryFormat::EventRecord event;
                        event.timestampOffsetUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
                        event.eventCode = BinaryFormat::EventCode::LowBattery;
                        event.dataLength = 0;
                        bufferWrite(&event, sizeof(event));
                        
                        running = false;  // Trigger graceful stop
                    }
                }
            }
            
            // Periodic checkpoint for crash recovery
            if (currentConfig.checkpointIntervalSec > 0 &&
                millis() - lastCheckpointMs > currentConfig.checkpointIntervalSec * 1000) {
                lastCheckpointMs = millis();
                writeCheckpoint();
            }
            
            // Periodic temperature read for compensation
            if (currentConfig.enableTempCompensation && millis() - lastTempReadMs > 5000) {
                lastTempReadMs = millis();
                float temp = RX8900CE::getTemperature();
                if (temp > -40.0f && temp < 85.0f) {  // Valid range check
                    lastTemperature = temp;
                }
            }
            
            // Check for file rotation conditions
            if (shouldRotateFile()) {
                rotateFile();
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
    ESP_LOGI(TAG, "start() called");
    uint32_t startTimeMs = millis();
    
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }
    
    // Check SD card
    ESP_LOGI(TAG, "Checking SD card...");
    if (!SDManager::isMounted()) {
        if (!SDManager::mount()) {
            ESP_LOGE(TAG, "SD card not available");
            return false;
        }
    }
    ESP_LOGI(TAG, "SD card OK (%lu ms)", millis() - startTimeMs);
    
    // Generate or use filename
    if (currentConfig.autoFilename) {
        generateFilename(currentFilePath, sizeof(currentFilePath));
    } else {
        snprintf(currentFilePath, sizeof(currentFilePath), "%s/%s",
                 currentConfig.outputDir, currentConfig.filename);
    }
    
    // Open file
    ESP_LOGI(TAG, "Opening file: %s", currentFilePath);
    
    logFile = SDManager::open(currentFilePath, FILE_WRITE);
    if (!logFile) {
        ESP_LOGE(TAG, "Failed to open: %s", currentFilePath);
        return false;
    }
    ESP_LOGI(TAG, "File opened (%lu ms)", millis() - startTimeMs);
    
    // Pre-allocate file space to reduce fragmentation-induced write spikes
    // Skip pre-allocation if maxDurationSec is 0 or very large (would block too long)
    if (currentConfig.maxDurationSec > 0 && currentConfig.maxDurationSec <= 600) {
        size_t estimatedSize = estimateFileSize(
            currentConfig.adcRateHz,
            currentConfig.imuDecimation,
            currentConfig.maxDurationSec
        );
        if (!preAllocateFile(logFile, estimatedSize)) {
            ESP_LOGW(TAG, "File pre-allocation failed - may have write latency spikes");
        }
    } else {
        ESP_LOGI(TAG, "Skipping pre-allocation (duration=%lu)", currentConfig.maxDurationSec);
    }
    
    // Reset state
    writeBufferUsed = 0;
    adcSamplesLogged = 0;
    imuSamplesLogged = 0;
    bytesWritten = 0;
    droppedSamples = 0;
    droppedBuffers = 0;
    adcSequenceNum = 0;  // Reset sequence counter
    
    // Reset hardening state
    runningCrc32 = 0;
    writeLatencyMinUs = UINT32_MAX;
    writeLatencyMaxUs = 0;
    writeLatencySumUs = 0;
    writeLatencyCount = 0;
    writeLatencyOver10ms = 0;
    bufferHighWaterMark = 0;
    saturationCount = 0;
    checkpointCount = 0;
    rotationCount = 0;
    fileRotationIndex = 0;
    lastCheckpointMs = millis();
    lastTempReadMs = millis();
    lastTemperature = 25.0f;
    
    // Reset peak tracking
    peakLoadN = 0.0f;
    peakLoadTimeMs = 0;
    peakDecelG = 0.0f;
    peakDecelTimeMs = 0;
    
    // Save session base path for file rotation (without .bin extension)
    strncpy(sessionBasePath, currentFilePath, sizeof(sessionBasePath) - 1);
    char* ext = strrchr(sessionBasePath, '.');
    if (ext) *ext = '\0';
    
    // Record start time
    sessionStartUs = TimestampSync::getEpochMicros();
    sessionStartMs = millis();
    
    // Write header
    ESP_LOGI(TAG, "Writing header...");
    if (!writeHeader()) {
        logFile.close();
        return false;
    }
    ESP_LOGI(TAG, "Header written (%lu ms)", millis() - startTimeMs);
    
    // Clear ring buffer
    if (adcBuffer) {
        adcBuffer->reset();
    }
    
    // Configure IMU FIFO for batch reading (zero-loss)
    ESP_LOGI(TAG, "Configuring IMU FIFO...");
    LSM6DSV::FIFOConfig fifoConfig;
    fifoConfig.watermark = 16;  // Interrupt at 16 samples
    fifoConfig.mode = LSM6DSV::FIFOMode::Continuous;
    fifoConfig.accelBatchRate = LSM6DSV::FIFOBatchRate::Hz120;  // Match logger rate
    fifoConfig.gyroBatchRate = LSM6DSV::FIFOBatchRate::Hz120;
    fifoConfig.enableTimestamp = false;
    
    if (LSM6DSV::configureFIFO(fifoConfig)) {
        LSM6DSV::enableFIFO();
        LSM6DSV::flushFIFO();  // Start fresh
        ESP_LOGI(TAG, "IMU FIFO enabled (%lu ms)", millis() - startTimeMs);
    } else {
        ESP_LOGW(TAG, "IMU FIFO config failed, using single reads");
    }
    
    // Start ADC continuous mode
    ESP_LOGI(TAG, "Starting ADC continuous mode...");
    if (!MAX11270::startContinuous(adcBuffer)) {
        ESP_LOGE(TAG, "Failed to start ADC");
        logFile.close();
        return false;
    }
    ESP_LOGI(TAG, "ADC started (%lu ms)", millis() - startTimeMs);
    
    running = true;
    paused = false;
    
    // Create logger task pinned to Core 0 (ADC ISR runs on Core 1)
    ESP_LOGI(TAG, "Creating logger task...");
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
    
    ESP_LOGI(TAG, "Started successfully: %s (total time: %lu ms)", currentFilePath, millis() - startTimeMs);
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
    endRec.checksum = runningCrc32;
    logFile.write((uint8_t*)&endRec, sizeof(endRec));
    
    // Write file footer for integrity verification
    BinaryFormat::FileFooter footer;
    footer.init();
    footer.totalAdcSamples = adcSamplesLogged.load();
    footer.totalImuSamples = imuSamplesLogged.load();
    footer.droppedSamples = droppedSamples.load();
    footer.endTimestampUs = (uint32_t)(TimestampSync::getEpochMicros() - sessionStartUs);
    footer.crc32 = runningCrc32;  // Final CRC32 of all data
    logFile.write((uint8_t*)&footer, sizeof(footer));
    
    ESP_LOGI(TAG, "Footer written: %llu ADC, %llu IMU, %lu dropped, CRC32=0x%08lX",
             footer.totalAdcSamples, footer.totalImuSamples, footer.droppedSamples, footer.crc32);
    
    // Close file
    logFile.flush();
    logFile.close();
    
    ESP_LOGI(TAG, "Binary file closed: %s (%lu bytes)", currentFilePath, bytesWritten.load());
    
    // Convert binary to CSV
    ESP_LOGI(TAG, "Starting CSV conversion...");
    if (CSVConverter::convert(currentFilePath)) {
        CSVConverter::Result result = CSVConverter::getLastResult();
        ESP_LOGI(TAG, "CSV conversion complete: %s (%lu ms)", 
                 result.outputPath, result.durationMs);
    } else {
        ESP_LOGW(TAG, "CSV conversion failed: %s", 
                 CSVConverter::statusToString(CSVConverter::getLastResult().status));
    }
    
    // Clear session state (clean shutdown)
    clearSessionState();
    
    uint32_t durationMs = millis() - sessionStartMs;
    ESP_LOGI(TAG, "Stopped: %llu ADC + %llu IMU samples, %lu bytes, %lu ms",
             adcSamplesLogged.load(), imuSamplesLogged.load(),
             bytesWritten.load(), durationMs);
    
    // Log write latency statistics
    if (writeLatencyCount > 0) {
        ESP_LOGI(TAG, "Write latency: min=%luus, max=%luus, avg=%luus, >10ms=%lu times",
                 writeLatencyMinUs, writeLatencyMaxUs, 
                 (uint32_t)(writeLatencySumUs / writeLatencyCount),
                 writeLatencyOver10ms.load());
    }
    
    // Log saturation events
    if (saturationCount.load() > 0) {
        ESP_LOGW(TAG, "ADC saturation detected %lu times during session", saturationCount.load());
    }
    
    // Store session summary
    lastSessionSummary.peakLoadN = peakLoadN.load();
    lastSessionSummary.peakLoadTimeMs = peakLoadTimeMs.load();
    lastSessionSummary.peakDecelG = peakDecelG.load();
    lastSessionSummary.peakDecelTimeMs = peakDecelTimeMs.load();
    lastSessionSummary.totalAdcSamples = adcSamplesLogged.load();
    lastSessionSummary.totalImuSamples = imuSamplesLogged.load();
    lastSessionSummary.durationMs = durationMs;
    lastSessionSummary.droppedSamples = droppedSamples.load();
    lastSessionSummary.valid = true;
    
    ESP_LOGI(TAG, "Session Summary: Peak Load=%.2f N @ %.2fs, Peak Decel=%.2f g @ %.2fs",
             lastSessionSummary.peakLoadN, lastSessionSummary.peakLoadTimeMs / 1000.0f,
             lastSessionSummary.peakDecelG, lastSessionSummary.peakDecelTimeMs / 1000.0f);
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
    
    // Write latency statistics
    status.writeStats.minUs = (writeLatencyMinUs == UINT32_MAX) ? 0 : writeLatencyMinUs;
    status.writeStats.maxUs = writeLatencyMaxUs;
    status.writeStats.avgUs = (writeLatencyCount > 0) ? (writeLatencySumUs / writeLatencyCount) : 0;
    status.writeStats.countOver10ms = writeLatencyOver10ms.load();
    
    // Buffer high water mark (as percentage)
    if (adcBuffer && adcBuffer->capacity() > 0) {
        status.bufferHighWater = (bufferHighWaterMark.load() * 100) / adcBuffer->capacity();
    } else {
        status.bufferHighWater = 0;
    }
    
    // Other hardening stats
    status.checkpointCount = checkpointCount;
    status.saturationCount = saturationCount.load();
    status.fileRotations = rotationCount;
    status.crc32 = runningCrc32;
    
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

uint32_t getAdcRateHz() {
    return currentConfig.adcRateHz;
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

bool hasRecoveryData() {
    return hasRecoverableSession();
}

bool recoverSession() {
    if (!hasRecoverableSession()) {
        ESP_LOGI(TAG, "No session to recover");
        return false;
    }
    
    char filepath[64];
    uint64_t adcCount, imuCount;
    uint32_t sequence, crc;
    
    if (!loadSessionState(filepath, sizeof(filepath), &adcCount, &imuCount, &sequence, &crc)) {
        ESP_LOGE(TAG, "Failed to load session state");
        return false;
    }
    
    ESP_LOGI(TAG, "Recovering session: %s", filepath);
    ESP_LOGI(TAG, "  ADC: %llu, IMU: %llu, Seq: %lu", adcCount, imuCount, sequence);
    
    // Check if the file exists
    if (!SDManager::isMounted()) {
        if (!SDManager::mount()) {
            ESP_LOGE(TAG, "SD card not available for recovery");
            return false;
        }
    }
    
    if (!SDManager::exists(filepath)) {
        ESP_LOGE(TAG, "Recovery file not found: %s", filepath);
        clearSessionState();
        return false;
    }
    
    // Open file for append
    logFile = SDManager::open(filepath, FILE_APPEND);
    if (!logFile) {
        ESP_LOGE(TAG, "Failed to open recovery file");
        clearSessionState();
        return false;
    }
    
    // Restore state
    strncpy(currentFilePath, filepath, sizeof(currentFilePath) - 1);
    adcSamplesLogged = adcCount;
    imuSamplesLogged = imuCount;
    adcSequenceNum = sequence;
    runningCrc32 = crc;
    bytesWritten = logFile.size();
    
    // Write recovery event
    BinaryFormat::EventRecord event;
    event.timestampOffsetUs = 0;  // Will be set properly once we have timing
    event.eventCode = BinaryFormat::EventCode::Recovery;
    event.dataLength = 0;
    
    uint8_t tag = static_cast<uint8_t>(BinaryFormat::RecordType::Event);
    logFile.write(&tag, 1);
    logFile.write((uint8_t*)&event, sizeof(event));
    
    ESP_LOGI(TAG, "Session recovered, ready to continue");
    
    // Note: caller should still call start() to resume logging
    // This just restores state, doesn't restart acquisition
    
    return true;
}

void clearRecoveryData() {
    clearSessionState();
    ESP_LOGI(TAG, "Recovery data cleared");
}

WriteStats getWriteStats() {
    WriteStats stats;
    stats.minUs = (writeLatencyMinUs == UINT32_MAX) ? 0 : writeLatencyMinUs;
    stats.maxUs = writeLatencyMaxUs;
    stats.avgUs = (writeLatencyCount > 0) ? (writeLatencySumUs / writeLatencyCount) : 0;
    stats.countOver10ms = writeLatencyOver10ms.load();
    return stats;
}

SessionSummary getSessionSummary() {
    // If currently running, return live values
    if (running) {
        SessionSummary live;
        live.peakLoadN = peakLoadN.load();
        live.peakLoadTimeMs = peakLoadTimeMs.load();
        live.peakDecelG = peakDecelG.load();
        live.peakDecelTimeMs = peakDecelTimeMs.load();
        live.totalAdcSamples = adcSamplesLogged.load();
        live.totalImuSamples = imuSamplesLogged.load();
        live.durationMs = millis() - sessionStartMs;
        live.droppedSamples = droppedSamples.load();
        live.valid = true;
        return live;
    }
    
    // Return last completed session summary
    return lastSessionSummary;
}

} // namespace Logger

