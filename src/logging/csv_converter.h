/**
 * @file csv_converter.h
 * @brief Binary Log to CSV Converter
 * 
 * Converts binary log files to human-readable CSV format with
 * ADC and IMU data merged into a single file.
 */

#ifndef CSV_CONVERTER_H
#define CSV_CONVERTER_H

#include <Arduino.h>

namespace CSVConverter {

// ============================================================================
// Status and Progress
// ============================================================================

/** @brief Conversion status */
enum class Status {
    Idle,           // No conversion in progress
    Converting,     // Conversion running
    Success,        // Last conversion succeeded
    ErrorOpenInput, // Failed to open input file
    ErrorOpenOutput,// Failed to open/create output file
    ErrorInvalidHeader, // Invalid binary file header
    ErrorRead,      // Read error during conversion
    ErrorWrite      // Write error during conversion
};

/** @brief Conversion result info */
struct Result {
    Status status;
    uint32_t adcRecords;    // ADC records converted
    uint32_t imuRecords;    // IMU records converted
    uint32_t durationMs;    // Conversion time in ms
    char outputPath[64];    // Path to output CSV file
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Convert binary log file to CSV
 * 
 * Reads binary file, converts ADC to force using calibration,
 * and writes merged CSV with ADC and IMU data.
 * 
 * @param binPath Path to input binary file (e.g., "/data/log_123.bin")
 * @param csvPath Path to output CSV file (e.g., "/data/log_123.csv")
 *                If nullptr, auto-generates from binPath
 * @return true if conversion completed successfully
 */
bool convert(const char* binPath, const char* csvPath = nullptr);

/**
 * @brief Get conversion progress
 * 
 * @return Progress as 0.0 to 1.0 (0% to 100%)
 */
float getProgress();

/**
 * @brief Check if conversion is in progress
 * 
 * @return true if currently converting
 */
bool isConverting();

/**
 * @brief Get last conversion result
 * 
 * @return Result structure with status and statistics
 */
Result getLastResult();

/**
 * @brief Get status as human-readable string
 */
const char* statusToString(Status status);

} // namespace CSVConverter

#endif // CSV_CONVERTER_H





