/**
 * @file bin_to_csv.h
 * @brief Binary to CSV Converter
 * 
 * Converts binary log files to human-readable CSV format
 * with optional unit conversion using calibration data.
 */

#ifndef BIN_TO_CSV_H
#define BIN_TO_CSV_H

#include <Arduino.h>

namespace BinToCSV {

// ============================================================================
// Configuration
// ============================================================================

/** @brief Conversion options */
struct Options {
    bool includeHeader;         // Include CSV header row
    bool convertToPhysical;     // Convert raw values to physical units
    bool includeTimestamp;      // Include absolute timestamp
    bool separateIMU;           // Write IMU data to separate file
    uint32_t decimation;        // Decimate ADC samples (1 = no decimation)
};

/** @brief Default options */
inline Options defaultOptions() {
    return {
        .includeHeader = true,
        .convertToPhysical = true,
        .includeTimestamp = true,
        .separateIMU = false,
        .decimation = 1
    };
}

// ============================================================================
// Progress
// ============================================================================

/** @brief Conversion progress */
struct Progress {
    bool running;
    uint32_t totalRecords;
    uint32_t processedRecords;
    uint8_t percentComplete;
    uint32_t bytesRead;
    uint32_t bytesWritten;
    char status[32];            // Status message
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Convert binary file to CSV (blocking)
 * 
 * @param binPath Path to binary input file
 * @param csvPath Path to CSV output file (nullptr = auto-generate)
 * @param options Conversion options
 * @return true if conversion successful
 */
bool convert(const char* binPath, const char* csvPath = nullptr,
             const Options& options = defaultOptions());

/**
 * @brief Start async conversion
 * 
 * @param binPath Path to binary input file
 * @param csvPath Path to CSV output file
 * @param options Conversion options
 * @return true if conversion started
 */
bool startAsync(const char* binPath, const char* csvPath = nullptr,
                const Options& options = defaultOptions());

/**
 * @brief Get conversion progress
 */
Progress getProgress();

/**
 * @brief Cancel ongoing conversion
 */
void cancel();

/**
 * @brief Check if conversion is running
 */
bool isRunning();

/**
 * @brief Wait for conversion to complete
 * 
 * @param timeoutMs Timeout in milliseconds
 * @return true if completed, false if timeout or cancelled
 */
bool waitComplete(uint32_t timeoutMs = 60000);

/**
 * @brief Get last error message
 */
const char* getLastError();

/**
 * @brief Generate CSV filename from binary filename
 * 
 * Replaces .bin extension with .csv
 * 
 * @param binPath Binary file path
 * @param csvPath Output buffer
 * @param maxLen Buffer size
 */
void generateCsvPath(const char* binPath, char* csvPath, size_t maxLen);

} // namespace BinToCSV

#endif // BIN_TO_CSV_H

