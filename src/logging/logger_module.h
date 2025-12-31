/**
 * @file logger_module.h
 * @brief High-rate data acquisition and buffered logging to SD card
 */

#ifndef LOGGER_MODULE_H
#define LOGGER_MODULE_H

#include <Arduino.h>
#include "binary_format.h"
#include "timestamp_sync.h"
#include "../drivers/max11270_driver.h"
#include "../drivers/lsm6dsv_driver.h"
#include "../drivers/sd_manager.h"
#include "../calibration/loadcell_types.h"

/** @brief Ring buffer size for ADC samples (32KB) */
#define RING_BUFFER_SIZE (32 * 1024)

/** @brief Write buffer size (8KB each, double buffered) */
#define WRITE_BUFFER_SIZE (8 * 1024)

/** @brief IMU decimation ratio (1 IMU sample per N ADC samples) */
#define IMU_DECIMATION 64

/** @brief Maximum ring buffer entries */
#define MAX_RING_ENTRIES 1024

/**
 * @brief High-rate data logger with double-buffered writes
 * 
 * Uses dual-core architecture:
 * - Core 1: ADC ISR, IMU sync read, ring buffer fill
 * - Core 0: Buffer drain, SD card writes
 */
class LoggerModule {
public:
    /**
     * @brief Initialize the logger
     * @param adc Pointer to ADC driver
     * @param imu Pointer to IMU driver
     * @param sd Pointer to SD manager
     * @param ts Pointer to timestamp sync
     * @return true if successful
     */
    bool begin(MAX11270Driver* adc, LSM6DSVDriver* imu, 
               SDManager* sd, TimestampSync* ts);
    
    /**
     * @brief Start logging session
     * @param loadcell_cal Active loadcell calibration data
     * @return true if successful, false if error
     */
    bool startLogging(const LoadcellCalibration& loadcell_cal);
    
    /**
     * @brief Stop logging session
     * @return true if successful
     */
    bool stopLogging();
    
    /**
     * @brief Check if currently logging
     * @return true if active
     */
    bool isLogging() const { return logging; }
    
    /**
     * @brief Get path to current log file
     * @return File path string
     */
    String getCurrentLogFile() const { return current_log_file; }
    
    /**
     * @brief Get logging statistics
     */
    struct Stats {
        uint32_t samples_acquired;
        uint32_t samples_written;
        uint32_t imu_samples;
        uint32_t buffer_overruns;
        uint32_t write_errors;
        float fill_percent;
    };
    
    Stats getStats() const { return stats; }
    
private:
    // Hardware drivers
    MAX11270Driver* adc_driver;
    LSM6DSVDriver* imu_driver;
    SDManager* sd_manager;
    TimestampSync* timestamp_sync;
    
    // Logging state
    volatile bool logging;
    String current_log_file;
    File log_file;
    Stats stats;
    
    // Ring buffer (lock-free SPSC)
    struct RingBufferEntry {
        uint32_t timestamp_us;
        int32_t adc_value;
        bool has_imu;
        IMUSample imu;
    };
    
    RingBufferEntry ring_buffer[MAX_RING_ENTRIES];
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    
    // Double write buffers
    uint8_t write_buffer_a[WRITE_BUFFER_SIZE];
    uint8_t write_buffer_b[WRITE_BUFFER_SIZE];
    uint8_t* active_buffer;
    size_t active_buffer_pos;
    bool buffer_a_active;
    
    // ISR state
    volatile uint32_t adc_sample_count;
    TaskHandle_t writer_task_handle;
    
    /**
     * @brief ADC data ready ISR (Core 1)
     */
    static void IRAM_ATTR adcReadyISR();
    
    /**
     * @brief Buffer writer task (Core 0)
     */
    static void writerTask(void* param);
    
    /**
     * @brief Drain ring buffer to write buffer
     */
    void drainRingBuffer();
    
    /**
     * @brief Flush write buffer to SD card
     */
    bool flushWriteBuffer();
    
    /**
     * @brief Generate unique log filename
     */
    String generateLogFilename();
    
    /**
     * @brief Write file header
     */
    bool writeHeader(const LoadcellCalibration& cal);
    
    /**
     * @brief Get ring buffer fill level
     */
    uint32_t getRingBufferFill() const;
    
    static LoggerModule* instance;  ///< Singleton for ISR
};

#endif // LOGGER_MODULE_H
