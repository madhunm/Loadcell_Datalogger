/**
 * @file logger_module.h
 * @brief High-Rate Data Logger Module
 * 
 * Coordinates ADC and IMU acquisition, timestamps, and buffered
 * writes to SD card using the binary format.
 * 
 * Architecture:
 *   - Core 1: ADC ISR + IMU sync reads -> Ring buffer
 *   - Core 0: Ring buffer drain -> Double buffer -> SD writes
 */

#ifndef LOGGER_MODULE_H
#define LOGGER_MODULE_H

#include <Arduino.h>
#include "binary_format.h"

namespace Logger {

// ============================================================================
// Configuration
// ============================================================================

/** @brief Logger configuration */
struct Config {
    uint32_t adcRateHz;             // Target ADC sample rate (e.g., 64000)
    uint32_t imuDecimation;         // IMU reads every N ADC samples (e.g., 64)
    const char* outputDir;          // Output directory (e.g., "/data")
    bool autoFilename;              // Generate filename from timestamp
    const char* filename;           // Manual filename (if not auto)
    size_t bufferSizeKB;            // Buffer size in KB (default 8)
    uint32_t maxDurationSec;        // Max log duration for pre-allocation (default 3600 = 1hr)
    
    // Hardening options
    uint32_t checkpointIntervalSec; // Checkpoint frequency (0 = disabled, default 30)
    uint32_t maxFileSizeMB;         // File rotation size limit (0 = no limit)
    uint32_t maxFileDurationSec;    // File rotation time limit (0 = no limit)
    bool enableCrc32;               // Compute CRC32 (slight CPU cost)
    bool enableTempCompensation;    // Temperature drift correction
    float tempCoefficient;          // Temperature coefficient (ppm/degC)
};

/** @brief Default configuration */
inline Config defaultConfig() {
    return {
        .adcRateHz = 64000,
        .imuDecimation = 64,
        .outputDir = "/data",
        .autoFilename = true,
        .filename = nullptr,
        .bufferSizeKB = 8,
        .maxDurationSec = 3600,         // 1 hour default pre-allocation
        .checkpointIntervalSec = 30,    // Checkpoint every 30 seconds
        .maxFileSizeMB = 0,             // No size limit by default
        .maxFileDurationSec = 0,        // No time limit by default
        .enableCrc32 = true,            // Enable CRC32 by default
        .enableTempCompensation = false,// Disabled until calibrated
        .tempCoefficient = -0.0005f     // Default temp coeff (ppm/degC)
    };
}

// ============================================================================
// Status
// ============================================================================

/** @brief Write latency statistics */
struct WriteStats {
    uint32_t minUs;             // Minimum write latency
    uint32_t maxUs;             // Maximum write latency
    uint32_t avgUs;             // Average write latency
    uint32_t countOver10ms;     // Writes exceeding 10ms threshold
};

/** @brief Session summary with peak values */
struct SessionSummary {
    float peakLoadN;            // Maximum load value in Newtons
    uint32_t peakLoadTimeMs;    // Time of peak load (ms from start)
    float peakDecelG;           // Maximum deceleration (g)
    uint32_t peakDecelTimeMs;   // Time of peak decel (ms from start)
    uint64_t totalAdcSamples;   // Total ADC samples logged
    uint64_t totalImuSamples;   // Total IMU samples logged
    uint32_t durationMs;        // Session duration
    uint32_t droppedSamples;    // Samples lost
    bool valid;                 // True if summary contains valid data
};

/** @brief Logger status */
struct Status {
    bool initialized;
    bool running;
    uint64_t samplesLogged;     // ADC samples written
    uint64_t imuSamplesLogged;  // IMU samples written
    uint32_t bytesWritten;
    uint32_t droppedSamples;    // Ring buffer overflows
    uint32_t droppedBuffers;    // SD write overflows
    float fillPercent;          // Ring buffer fill level
    uint32_t durationMs;        // Recording duration
    char currentFile[64];       // Current output filename
    
    // Hardening status
    WriteStats writeStats;      // SD write latency stats
    uint32_t bufferHighWater;   // Ring buffer high-water mark (%)
    uint32_t checkpointCount;   // Number of checkpoints written
    uint32_t saturationCount;   // ADC saturation events
    uint32_t fileRotations;     // Number of file rotations
    uint32_t crc32;             // Current running CRC32
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize the logger module
 * 
 * Sets up ring buffer, prepares SD card writing.
 * Does NOT start logging - call start() for that.
 * 
 * @param config Logger configuration
 * @return true if initialization successful
 */
bool init(const Config& config);

/**
 * @brief Initialize with default configuration
 */
bool init();

/**
 * @brief Check if logger is initialized
 */
bool isInitialized();

/**
 * @brief Start a logging session
 * 
 * Creates output file, writes header, starts acquisition.
 * 
 * @return true if started successfully
 */
bool start();

/**
 * @brief Stop the logging session
 * 
 * Flushes buffers, writes end marker, closes file.
 */
void stop();

/**
 * @brief Check if logging is active
 */
bool isRunning();

/**
 * @brief Get current status
 */
Status getStatus();

/**
 * @brief Get current output file path
 * 
 * @return Full path to current/last output file
 */
const char* getCurrentFilePath();

/**
 * @brief Update function (call from main loop)
 * 
 * Processes ring buffer, manages SD writes.
 * Should be called frequently when logging.
 */
void update();

/**
 * @brief Set loadcell ID for file header
 * 
 * @param id Loadcell ID string
 */
void setLoadcellId(const char* id);

// ============================================================================
// Advanced API
// ============================================================================

/**
 * @brief Pause logging (keep file open)
 */
void pause();

/**
 * @brief Resume logging after pause
 */
void resume();

/**
 * @brief Check if paused
 */
bool isPaused();

/**
 * @brief Write an event marker to the log
 * 
 * @param eventCode Event code (see BinaryFormat::EventCode)
 * @param data Optional event data
 * @param dataLen Length of event data
 */
void writeEvent(uint16_t eventCode, const uint8_t* data = nullptr, size_t dataLen = 0);

/**
 * @brief Get ring buffer statistics
 * 
 * @param capacity Output: buffer capacity
 * @param used Output: bytes currently used
 * @param overflows Output: overflow count
 */
void getRingBufferStats(size_t* capacity, size_t* used, uint32_t* overflows);

/**
 * @brief Force flush all buffers to SD
 * 
 * Blocks until all data is written.
 * 
 * @param timeoutMs Timeout in milliseconds
 * @return true if all data flushed
 */
bool flush(uint32_t timeoutMs = 5000);

// ============================================================================
// Recovery API
// ============================================================================

/**
 * @brief Check if there's recoverable session data from a crash
 * 
 * @return true if there's a session that can be recovered
 */
bool hasRecoveryData();

/**
 * @brief Recover a crashed session
 * 
 * Restores session state from NVS and prepares to continue logging.
 * Call start() after this to resume acquisition.
 * 
 * @return true if recovery successful
 */
bool recoverSession();

/**
 * @brief Clear any recovery data (acknowledge clean state)
 */
void clearRecoveryData();

/**
 * @brief Get write latency statistics
 * 
 * @return Current write statistics
 */
WriteStats getWriteStats();

/**
 * @brief Get session summary with peak values
 * 
 * Returns summary from current or last completed session.
 * 
 * @return Session summary with peak load (N) and deceleration (g)
 */
SessionSummary getSessionSummary();

} // namespace Logger

#endif // LOGGER_MODULE_H

