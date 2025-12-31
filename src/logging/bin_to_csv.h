/**
 * @file bin_to_csv.h
 * @brief On-device converter from binary log format to CSV
 */

#ifndef BIN_TO_CSV_H
#define BIN_TO_CSV_H

#include <Arduino.h>
#include "binary_format.h"
#include "../drivers/sd_manager.h"
#include "../drivers/lsm6dsv_driver.h"
#include "../drivers/rx8900ce_driver.h"
#include "../calibration/calibration_interp.h"

/**
 * @brief Converts binary log files to human-readable CSV format
 */
class BinToCSVConverter {
public:
    /**
     * @brief Initialize converter
     * @param sd Pointer to SD manager
     * @param interp Pointer to calibration interpolator
     * @return true if successful
     */
    bool begin(SDManager* sd, CalibrationInterp* interp);
    
    /**
     * @brief Convert a binary log file to CSV
     * @param bin_path Path to binary log file
     * @param progress_callback Optional callback for progress updates (0-100)
     * @return true if successful, false on error
     */
    bool convert(const char* bin_path, void (*progress_callback)(int) = nullptr);
    
    /**
     * @brief Get path to last generated CSV file
     * @return CSV file path
     */
    String getLastCSVPath() const { return last_csv_path; }
    
    /**
     * @brief Get conversion statistics
     */
    struct ConversionStats {
        uint32_t loadcell_samples;
        uint32_t imu_samples;
        uint32_t bytes_read;
        uint32_t bytes_written;
        uint32_t duration_ms;
    };
    
    ConversionStats getStats() const { return stats; }
    
private:
    SDManager* sd_manager;
    CalibrationInterp* calibration_interp;
    String last_csv_path;
    ConversionStats stats;
    
    /**
     * @brief Generate CSV filename from binary filename
     */
    String generateCSVFilename(const char* bin_path);
    
    /**
     * @brief Write CSV header
     */
    bool writeCSVHeader(File& csv_file, const LogFileHeader& header);
    
    /**
     * @brief Convert timestamp to ISO 8601 string
     */
    String timestampToISO(uint64_t timestamp_us);
};

#endif // BIN_TO_CSV_H
