/**
 * @file bin_to_csv.cpp
 * @brief Binary to CSV Converter Implementation
 */

#include "bin_to_csv.h"
#include "binary_format.h"
#include "../drivers/sd_manager.h"
#include "../calibration/calibration_interp.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstring>

static const char* TAG = "BinToCSV";

namespace BinToCSV {

namespace {
    // State
    std::atomic<bool> running{false};
    std::atomic<bool> cancelled{false};
    
    // Progress
    Progress progress = {0};
    char lastError[64] = {0};
    
    // Async task
    TaskHandle_t conversionTask = nullptr;
    
    // Task parameters
    struct TaskParams {
        char binPath[64];
        char csvPath[64];
        Options options;
    };
    TaskParams taskParams;
    
    // Write CSV header
    bool writeHeader(File& file, const BinaryFormat::FileHeader& header, const Options& options) {
        char line[256];
        int len;
        
        if (options.includeTimestamp) {
            if (options.convertToPhysical) {
                len = snprintf(line, sizeof(line), 
                    "timestamp_us,time_offset_us,load_kg,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps\n");
            } else {
                len = snprintf(line, sizeof(line),
                    "timestamp_us,time_offset_us,raw_adc,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z\n");
            }
        } else {
            if (options.convertToPhysical) {
                len = snprintf(line, sizeof(line),
                    "time_offset_us,load_kg,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps\n");
            } else {
                len = snprintf(line, sizeof(line),
                    "time_offset_us,raw_adc,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z\n");
            }
        }
        
        return file.write((uint8_t*)line, len) == (size_t)len;
    }
    
    // Process conversion
    bool doConvert(const char* binPath, const char* csvPath, const Options& options) {
        // Open input file
        File binFile = SDManager::open(binPath, FILE_READ);
        if (!binFile) {
            snprintf(lastError, sizeof(lastError), "Cannot open input: %s", binPath);
            ESP_LOGE(TAG, "%s", lastError);
            return false;
        }
        
        // Read header
        BinaryFormat::FileHeader header;
        if (binFile.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
            snprintf(lastError, sizeof(lastError), "Failed to read header");
            binFile.close();
            return false;
        }
        
        // Validate header
        if (!header.isValid()) {
            snprintf(lastError, sizeof(lastError), "Invalid file format");
            binFile.close();
            return false;
        }
        
        // Calculate total records estimate
        size_t fileSize = binFile.size();
        size_t dataSize = fileSize - sizeof(header);
        // Rough estimate: mostly ADC records (8 bytes each)
        progress.totalRecords = dataSize / 8;
        
        // Open output file
        File csvFile = SDManager::open(csvPath, FILE_WRITE);
        if (!csvFile) {
            snprintf(lastError, sizeof(lastError), "Cannot create output: %s", csvPath);
            binFile.close();
            return false;
        }
        
        // Write CSV header
        if (options.includeHeader) {
            if (!writeHeader(csvFile, header, options)) {
                snprintf(lastError, sizeof(lastError), "Failed to write CSV header");
                binFile.close();
                csvFile.close();
                return false;
            }
        }
        
        // Conversion state
        uint64_t baseTimestamp = header.startTimestampUs;
        uint32_t decimationCounter = 0;
        
        // Last IMU data for interpolation
        BinaryFormat::IMURecord lastIMU = {0};
        bool haveIMU = false;
        
        // Buffer for CSV lines
        char line[256];
        
        // Process records
        uint8_t recordBuf[32];
        
        strncpy(progress.status, "Converting", sizeof(progress.status));
        
        while (!cancelled && binFile.available() > 0) {
            // Read record (peek first byte for type or read ADC/IMU directly)
            size_t pos = binFile.position();
            
            // Try reading as ADC record
            if (binFile.read(recordBuf, sizeof(BinaryFormat::ADCRecord)) != sizeof(BinaryFormat::ADCRecord)) {
                break;  // End of file
            }
            
            BinaryFormat::ADCRecord* adcRec = (BinaryFormat::ADCRecord*)recordBuf;
            
            // Check if this looks like an end marker
            if (recordBuf[0] == 0xFF) {
                break;  // End marker
            }
            
            progress.processedRecords++;
            progress.bytesRead += sizeof(BinaryFormat::ADCRecord);
            
            // Apply decimation
            decimationCounter++;
            if (options.decimation > 1 && (decimationCounter % options.decimation) != 0) {
                continue;
            }
            
            // Build CSV line
            int len;
            uint64_t absoluteTs = baseTimestamp + adcRec->timestampOffsetUs;
            
            if (options.convertToPhysical && CalibrationInterp::isReady()) {
                float loadKg = CalibrationInterp::rawToKg(adcRec->rawAdc);
                
                if (options.includeTimestamp) {
                    len = snprintf(line, sizeof(line), "%llu,%lu,%.4f",
                        absoluteTs, adcRec->timestampOffsetUs, loadKg);
                } else {
                    len = snprintf(line, sizeof(line), "%lu,%.4f",
                        adcRec->timestampOffsetUs, loadKg);
                }
            } else {
                if (options.includeTimestamp) {
                    len = snprintf(line, sizeof(line), "%llu,%lu,%ld",
                        absoluteTs, adcRec->timestampOffsetUs, (long)adcRec->rawAdc);
                } else {
                    len = snprintf(line, sizeof(line), "%lu,%ld",
                        adcRec->timestampOffsetUs, (long)adcRec->rawAdc);
                }
            }
            
            // Add IMU data (if available)
            if (haveIMU) {
                if (options.convertToPhysical) {
                    // Convert to g and dps (assuming ±2g and ±250dps scales)
                    float ax = lastIMU.accelX * 0.061f / 1000.0f;
                    float ay = lastIMU.accelY * 0.061f / 1000.0f;
                    float az = lastIMU.accelZ * 0.061f / 1000.0f;
                    float gx = lastIMU.gyroX * 8.75f / 1000.0f;
                    float gy = lastIMU.gyroY * 8.75f / 1000.0f;
                    float gz = lastIMU.gyroZ * 8.75f / 1000.0f;
                    
                    len += snprintf(line + len, sizeof(line) - len,
                        ",%.4f,%.4f,%.4f,%.2f,%.2f,%.2f\n",
                        ax, ay, az, gx, gy, gz);
                } else {
                    len += snprintf(line + len, sizeof(line) - len,
                        ",%d,%d,%d,%d,%d,%d\n",
                        lastIMU.accelX, lastIMU.accelY, lastIMU.accelZ,
                        lastIMU.gyroX, lastIMU.gyroY, lastIMU.gyroZ);
                }
            } else {
                // No IMU data yet - empty columns
                len += snprintf(line + len, sizeof(line) - len, ",,,,,,\n");
            }
            
            // Write line
            if (csvFile.write((uint8_t*)line, len) != (size_t)len) {
                snprintf(lastError, sizeof(lastError), "Write error");
                break;
            }
            progress.bytesWritten += len;
            
            // Update progress
            if (progress.totalRecords > 0) {
                progress.percentComplete = (progress.processedRecords * 100) / progress.totalRecords;
            }
            
            // Check for IMU record (follows ADC records at decimation interval)
            // This is a simplification - in real implementation, records would be tagged
            // For now, assume IMU records follow every N ADC records based on header
            if (header.imuSampleRateHz > 0) {
                uint32_t imuDecimation = header.adcSampleRateHz / header.imuSampleRateHz;
                if (imuDecimation > 0 && (progress.processedRecords % imuDecimation) == 0) {
                    // Try to read IMU record
                    if (binFile.read((uint8_t*)&lastIMU, sizeof(lastIMU)) == sizeof(lastIMU)) {
                        haveIMU = true;
                        progress.bytesRead += sizeof(lastIMU);
                    }
                }
            }
            
            // Yield periodically
            if ((progress.processedRecords % 1000) == 0) {
                vTaskDelay(1);
            }
        }
        
        // Close files
        binFile.close();
        csvFile.flush();
        csvFile.close();
        
        if (cancelled) {
            strncpy(progress.status, "Cancelled", sizeof(progress.status));
            strncpy(lastError, "Cancelled by user", sizeof(lastError));
            return false;
        }
        
        strncpy(progress.status, "Complete", sizeof(progress.status));
        progress.percentComplete = 100;
        
        ESP_LOGI(TAG, "Conversion complete: %lu records, %lu bytes",
                 progress.processedRecords, progress.bytesWritten);
        return true;
    }
    
    // Async task function
    void conversionTaskFunc(void* param) {
        TaskParams* params = (TaskParams*)param;
        
        doConvert(params->binPath, params->csvPath, params->options);
        
        running = false;
        vTaskDelete(nullptr);
    }
}

// ============================================================================
// Public API
// ============================================================================

bool convert(const char* binPath, const char* csvPath, const Options& options) {
    if (running) {
        strncpy(lastError, "Conversion already in progress", sizeof(lastError));
        return false;
    }
    
    // Generate CSV path if not provided
    char generatedPath[64];
    if (!csvPath) {
        generateCsvPath(binPath, generatedPath, sizeof(generatedPath));
        csvPath = generatedPath;
    }
    
    // Reset progress
    memset(&progress, 0, sizeof(progress));
    progress.running = true;
    running = true;
    cancelled = false;
    
    bool result = doConvert(binPath, csvPath, options);
    
    running = false;
    progress.running = false;
    
    return result;
}

bool startAsync(const char* binPath, const char* csvPath, const Options& options) {
    if (running) {
        strncpy(lastError, "Conversion already in progress", sizeof(lastError));
        return false;
    }
    
    // Store parameters
    strncpy(taskParams.binPath, binPath, sizeof(taskParams.binPath) - 1);
    
    if (csvPath) {
        strncpy(taskParams.csvPath, csvPath, sizeof(taskParams.csvPath) - 1);
    } else {
        generateCsvPath(binPath, taskParams.csvPath, sizeof(taskParams.csvPath));
    }
    
    taskParams.options = options;
    
    // Reset progress
    memset(&progress, 0, sizeof(progress));
    progress.running = true;
    running = true;
    cancelled = false;
    strncpy(progress.status, "Starting", sizeof(progress.status));
    
    // Start task
    BaseType_t result = xTaskCreatePinnedToCore(
        conversionTaskFunc,
        "bin2csv",
        8192,
        &taskParams,
        3,
        &conversionTask,
        0  // Core 0
    );
    
    if (result != pdPASS) {
        running = false;
        progress.running = false;
        strncpy(lastError, "Failed to create task", sizeof(lastError));
        return false;
    }
    
    return true;
}

Progress getProgress() {
    return progress;
}

void cancel() {
    if (running) {
        cancelled = true;
        ESP_LOGI(TAG, "Cancellation requested");
    }
}

bool isRunning() {
    return running;
}

bool waitComplete(uint32_t timeoutMs) {
    uint32_t start = millis();
    
    while (running && (millis() - start) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return !running;
}

const char* getLastError() {
    return lastError;
}

void generateCsvPath(const char* binPath, char* csvPath, size_t maxLen) {
    strncpy(csvPath, binPath, maxLen - 1);
    csvPath[maxLen - 1] = '\0';
    
    // Find and replace .bin with .csv
    char* dot = strrchr(csvPath, '.');
    if (dot && strcmp(dot, ".bin") == 0) {
        strcpy(dot, ".csv");
    } else {
        // No .bin extension - append .csv
        size_t len = strlen(csvPath);
        if (len + 4 < maxLen) {
            strcat(csvPath, ".csv");
        }
    }
}

} // namespace BinToCSV

