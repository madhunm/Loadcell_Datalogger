/**
 * @file csv_converter.cpp
 * @brief Binary Log to CSV Converter Implementation
 */

#include "csv_converter.h"
#include "binary_format.h"
#include "../calibration/calibration_interp.h"
#include "../drivers/sd_manager.h"
#include <SD_MMC.h>

namespace CSVConverter {

// ============================================================================
// Private State
// ============================================================================

namespace {
    volatile bool converting = false;
    volatile float progress = 0.0f;
    Result lastResult = {Status::Idle, 0, 0, 0, ""};
    
    // IMU conversion constants
    // LSM6DSV at ±2g: 0.061 mg/LSB
    constexpr float ACCEL_SCALE = 0.061f / 1000.0f;  // Convert to g
    // LSM6DSV at ±125 dps: 4.375 mdps/LSB
    constexpr float GYRO_SCALE = 4.375f / 1000.0f;   // Convert to dps
    
    // Generate CSV path from binary path
    void generateCsvPath(const char* binPath, char* csvPath, size_t maxLen) {
        strncpy(csvPath, binPath, maxLen - 1);
        csvPath[maxLen - 1] = '\0';
        
        // Replace .bin with .csv
        char* ext = strrchr(csvPath, '.');
        if (ext && strcmp(ext, ".bin") == 0) {
            strcpy(ext, ".csv");
        } else {
            // Append .csv if no .bin extension
            strncat(csvPath, ".csv", maxLen - strlen(csvPath) - 1);
        }
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool convert(const char* binPath, const char* csvPath) {
    if (converting) {
        return false;  // Already converting
    }
    
    converting = true;
    progress = 0.0f;
    uint32_t startMs = millis();
    
    // Reset result
    lastResult.status = Status::Converting;
    lastResult.adcRecords = 0;
    lastResult.imuRecords = 0;
    lastResult.durationMs = 0;
    lastResult.outputPath[0] = '\0';
    
    // Generate output path if not provided
    char outputPath[64];
    if (csvPath == nullptr) {
        generateCsvPath(binPath, outputPath, sizeof(outputPath));
    } else {
        strncpy(outputPath, csvPath, sizeof(outputPath) - 1);
        outputPath[sizeof(outputPath) - 1] = '\0';
    }
    strncpy(lastResult.outputPath, outputPath, sizeof(lastResult.outputPath) - 1);
    
    Serial.printf("[CSVConverter] Converting %s -> %s\n", binPath, outputPath);
    
    // Open input binary file
    File binFile = SD_MMC.open(binPath, FILE_READ);
    if (!binFile) {
        Serial.printf("[CSVConverter] ERROR: Cannot open input: %s\n", binPath);
        lastResult.status = Status::ErrorOpenInput;
        converting = false;
        return false;
    }
    
    size_t fileSize = binFile.size();
    Serial.printf("[CSVConverter] Input file size: %zu bytes\n", fileSize);
    
    // Read and validate header
    BinaryFormat::FileHeader header;
    if (binFile.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        Serial.println("[CSVConverter] ERROR: Failed to read header");
        binFile.close();
        lastResult.status = Status::ErrorRead;
        converting = false;
        return false;
    }
    
    if (!header.isValid()) {
        Serial.println("[CSVConverter] ERROR: Invalid file header");
        binFile.close();
        lastResult.status = Status::ErrorInvalidHeader;
        converting = false;
        return false;
    }
    
    Serial.printf("[CSVConverter] Header valid: ADC=%lu Hz, IMU=%lu Hz, ID=%s\n",
                  header.adcSampleRateHz, header.imuSampleRateHz, header.loadcellId);
    
    // Open output CSV file
    File csvFile = SD_MMC.open(outputPath, FILE_WRITE);
    if (!csvFile) {
        Serial.printf("[CSVConverter] ERROR: Cannot create output: %s\n", outputPath);
        binFile.close();
        lastResult.status = Status::ErrorOpenOutput;
        converting = false;
        return false;
    }
    
    // Write CSV header
    csvFile.println("timestamp_ms,adc_raw,force_N,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps");
    
    // Calculate IMU decimation (how many ADC samples per IMU sample)
    uint32_t imuDecimation = header.imuSampleRateHz > 0 ? 
                             (header.adcSampleRateHz / header.imuSampleRateHz) : 0;
    
    // Process records
    uint32_t adcCount = 0;
    uint32_t imuCount = 0;
    size_t bytesRead = sizeof(header);
    char lineBuf[256];
    
    // Track last IMU data for merged output
    float lastAccelX = 0, lastAccelY = 0, lastAccelZ = 0;
    float lastGyroX = 0, lastGyroY = 0, lastGyroZ = 0;
    bool hasImuData = false;
    
    while (binFile.available() > 0) {
        // Update progress
        bytesRead = binFile.position();
        progress = (float)bytesRead / (float)fileSize;
        
        // Check for end of data (footer area)
        if (fileSize - bytesRead <= sizeof(BinaryFormat::FileFooter) + 16) {
            break;
        }
        
        // Read ADC record
        BinaryFormat::ADCRecord adc;
        if (binFile.read((uint8_t*)&adc, sizeof(adc)) != sizeof(adc)) {
            break;
        }
        
        // Check for end marker
        if (*((uint8_t*)&adc) == 0xFF) {
            break;
        }
        
        adcCount++;
        
        // Check if IMU record follows (based on decimation)
        if (imuDecimation > 0 && adcCount % imuDecimation == 0) {
            BinaryFormat::IMURecord imu;
            if (binFile.read((uint8_t*)&imu, sizeof(imu)) == sizeof(imu)) {
                // Convert IMU data to physical units
                lastAccelX = imu.accelX * ACCEL_SCALE;
                lastAccelY = imu.accelY * ACCEL_SCALE;
                lastAccelZ = imu.accelZ * ACCEL_SCALE;
                lastGyroX = imu.gyroX * GYRO_SCALE;
                lastGyroY = imu.gyroY * GYRO_SCALE;
                lastGyroZ = imu.gyroZ * GYRO_SCALE;
                hasImuData = true;
                imuCount++;
            }
        }
        
        // Convert ADC to force
        float timestampMs = adc.timestampOffsetUs / 1000.0f;
        float forceN = 0.0f;
        
        // Use calibration if available
        if (CalibrationInterp::isReady()) {
            float kg = CalibrationInterp::rawToKg(adc.rawAdc);
            forceN = kg * 9.80665f;  // Convert kg to Newtons
        }
        
        // Write CSV line
        if (hasImuData && imuDecimation > 0 && adcCount % imuDecimation == 0) {
            // ADC + IMU line
            snprintf(lineBuf, sizeof(lineBuf), 
                     "%.3f,%ld,%.3f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f",
                     timestampMs, adc.rawAdc, forceN,
                     lastAccelX, lastAccelY, lastAccelZ,
                     lastGyroX, lastGyroY, lastGyroZ);
        } else {
            // ADC only line
            snprintf(lineBuf, sizeof(lineBuf), 
                     "%.3f,%ld,%.3f,,,,,,,",
                     timestampMs, adc.rawAdc, forceN);
        }
        
        if (csvFile.println(lineBuf) == 0) {
            Serial.println("[CSVConverter] ERROR: Write failed");
            lastResult.status = Status::ErrorWrite;
            break;
        }
        
        // Periodic progress logging (every 100k records)
        if (adcCount % 100000 == 0) {
            Serial.printf("[CSVConverter] Progress: %.1f%% (%lu ADC, %lu IMU)\n",
                          progress * 100.0f, adcCount, imuCount);
        }
        
        // Yield periodically to avoid watchdog timeout
        if (adcCount % 10000 == 0) {
            yield();
        }
    }
    
    // Flush and close files
    csvFile.flush();
    csvFile.close();
    binFile.close();
    
    // Update result
    lastResult.adcRecords = adcCount;
    lastResult.imuRecords = imuCount;
    lastResult.durationMs = millis() - startMs;
    
    if (lastResult.status == Status::Converting) {
        lastResult.status = Status::Success;
    }
    
    progress = 1.0f;
    converting = false;
    
    Serial.printf("[CSVConverter] Complete: %lu ADC + %lu IMU records in %lu ms\n",
                  adcCount, imuCount, lastResult.durationMs);
    Serial.printf("[CSVConverter] Output: %s\n", outputPath);
    
    return lastResult.status == Status::Success;
}

float getProgress() {
    return progress;
}

bool isConverting() {
    return converting;
}

Result getLastResult() {
    return lastResult;
}

const char* statusToString(Status status) {
    switch (status) {
        case Status::Idle:              return "Idle";
        case Status::Converting:        return "Converting";
        case Status::Success:           return "Success";
        case Status::ErrorOpenInput:    return "Cannot open input file";
        case Status::ErrorOpenOutput:   return "Cannot create output file";
        case Status::ErrorInvalidHeader: return "Invalid file header";
        case Status::ErrorRead:         return "Read error";
        case Status::ErrorWrite:        return "Write error";
        default:                        return "Unknown";
    }
}

} // namespace CSVConverter





