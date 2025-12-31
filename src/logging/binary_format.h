/**
 * @file binary_format.h
 * @brief Binary Log File Format Definitions
 * 
 * Defines the binary format for high-rate data logging:
 * - File header with metadata
 * - ADC sample records (8 bytes each)
 * - IMU sample records (16 bytes each)
 * 
 * File structure:
 *   [Header 64 bytes]
 *   [Record][Record][Record]...
 * 
 * Records are tagged with a type byte to allow mixed ADC/IMU data.
 */

#ifndef BINARY_FORMAT_H
#define BINARY_FORMAT_H

#include <Arduino.h>

namespace BinaryFormat {

// ============================================================================
// Magic Numbers and Version
// ============================================================================

/** @brief File magic number "LCLG" (LoadCell LoG) */
constexpr uint32_t FILE_MAGIC = 0x474C434C;  // "LCLG" in little-endian

/** @brief Current format version */
constexpr uint16_t FORMAT_VERSION = 1;

/** @brief Header size in bytes */
constexpr uint16_t HEADER_SIZE = 64;

// ============================================================================
// Record Types
// ============================================================================

/** @brief Record type tags */
enum class RecordType : uint8_t {
    ADC     = 0x01,     // ADC sample
    IMU     = 0x02,     // IMU sample (accel + gyro)
    Event   = 0x10,     // Event marker
    Comment = 0x20,     // Text comment
    End     = 0xFF      // End of file marker
};

// ============================================================================
// File Header (64 bytes)
// ============================================================================

/**
 * @brief Log file header structure
 * 
 * Written once at the start of each log file.
 * All multi-byte values are little-endian.
 */
struct __attribute__((packed)) FileHeader {
    // Identification (8 bytes)
    uint32_t magic;             // FILE_MAGIC
    uint16_t version;           // FORMAT_VERSION
    uint16_t headerSize;        // sizeof(FileHeader) = 64
    
    // Sampling configuration (8 bytes)
    uint32_t adcSampleRateHz;   // ADC sample rate (e.g., 64000)
    uint32_t imuSampleRateHz;   // IMU sample rate (e.g., 1000)
    
    // Timing (8 bytes)
    uint64_t startTimestampUs;  // Unix epoch microseconds at start
    
    // Loadcell identification (32 bytes)
    char loadcellId[32];        // e.g., "TC023L0-000025"
    
    // Reserved for future use (8 bytes)
    uint8_t flags;              // Bit flags (reserved)
    uint8_t adcGain;            // ADC gain setting
    uint8_t adcBits;            // ADC resolution (e.g., 24)
    uint8_t imuAccelScale;      // IMU accel scale (0=2g, 1=4g, etc.)
    uint8_t imuGyroScale;       // IMU gyro scale
    uint8_t reserved[3];        // Padding
    
    // Initialize with defaults
    void init() {
        magic = FILE_MAGIC;
        version = FORMAT_VERSION;
        headerSize = HEADER_SIZE;
        adcSampleRateHz = 64000;
        imuSampleRateHz = 1000;
        startTimestampUs = 0;
        memset(loadcellId, 0, sizeof(loadcellId));
        flags = 0;
        adcGain = 1;
        adcBits = 24;
        imuAccelScale = 0;
        imuGyroScale = 1;
        memset(reserved, 0, sizeof(reserved));
    }
    
    // Validate header
    bool isValid() const {
        return magic == FILE_MAGIC && 
               version == FORMAT_VERSION && 
               headerSize == HEADER_SIZE;
    }
};

static_assert(sizeof(FileHeader) == 64, "FileHeader must be 64 bytes");

// ============================================================================
// ADC Sample Record (8 bytes)
// ============================================================================

/**
 * @brief ADC sample record
 * 
 * Stores one 24-bit ADC reading with timestamp offset and sequence number.
 * Timestamp is offset from file header startTimestampUs.
 * Sequence number enables gap detection in post-processing.
 */
struct __attribute__((packed)) ADCRecord {
    uint32_t timestampOffsetUs; // Microseconds since file start
    int32_t rawAdc;             // 24-bit ADC value (sign-extended to 32)
    uint32_t sequenceNum;       // Monotonic counter for gap detection
    
    // Size constant
    static constexpr size_t SIZE = 12;
};

static_assert(sizeof(ADCRecord) == 12, "ADCRecord must be 12 bytes");

// ============================================================================
// IMU Sample Record (16 bytes)
// ============================================================================

/**
 * @brief IMU sample record
 * 
 * Stores 6-axis IMU data (accel + gyro) with timestamp.
 */
struct __attribute__((packed)) IMURecord {
    uint32_t timestampOffsetUs; // Microseconds since file start
    int16_t accelX;             // Accelerometer X (raw)
    int16_t accelY;             // Accelerometer Y (raw)
    int16_t accelZ;             // Accelerometer Z (raw)
    int16_t gyroX;              // Gyroscope X (raw)
    int16_t gyroY;              // Gyroscope Y (raw)
    int16_t gyroZ;              // Gyroscope Z (raw)
    
    // Size constant
    static constexpr size_t SIZE = 16;
};

static_assert(sizeof(IMURecord) == 16, "IMURecord must be 16 bytes");

// ============================================================================
// Tagged Record (for mixed streams)
// ============================================================================

/**
 * @brief Tagged record wrapper
 * 
 * Prefixes each record with a type tag for parsing mixed streams.
 */
struct __attribute__((packed)) TaggedADCRecord {
    uint8_t type;               // RecordType::ADC
    ADCRecord record;
    
    static constexpr size_t SIZE = 1 + ADCRecord::SIZE;
};

struct __attribute__((packed)) TaggedIMURecord {
    uint8_t type;               // RecordType::IMU
    IMURecord record;
    
    static constexpr size_t SIZE = 1 + IMURecord::SIZE;
};

// ============================================================================
// Event Record (variable length)
// ============================================================================

/**
 * @brief Event marker record
 * 
 * Marks significant events in the data stream.
 */
struct __attribute__((packed)) EventRecord {
    uint32_t timestampOffsetUs;
    uint16_t eventCode;         // Application-defined event code
    uint16_t dataLength;        // Length of optional data
    // Followed by dataLength bytes of event-specific data
    
    static constexpr size_t MIN_SIZE = 8;
};

// Event codes
namespace EventCode {
    constexpr uint16_t SessionStart     = 0x0001;
    constexpr uint16_t SessionEnd       = 0x0002;
    constexpr uint16_t ButtonPress      = 0x0010;
    constexpr uint16_t Overflow         = 0x0020;  // Buffer overflow
    constexpr uint16_t SyncLost         = 0x0030;  // RTC sync lost
    constexpr uint16_t SyncRestored     = 0x0031;  // RTC sync restored
    constexpr uint16_t CalibrationPoint = 0x0100;  // Calibration reference
    
    // Hardening event codes
    constexpr uint16_t Checkpoint       = 0x00F0;  // Periodic checkpoint marker
    constexpr uint16_t FileRotation     = 0x00F1;  // File rotation occurred
    constexpr uint16_t LowBattery       = 0x00F2;  // Low battery warning
    constexpr uint16_t Saturation       = 0x00F3;  // ADC saturation detected
    constexpr uint16_t WriteLatency     = 0x00F4;  // High write latency warning
    constexpr uint16_t Recovery         = 0x00F5;  // Session recovered from crash
}

// ============================================================================
// End of File Marker
// ============================================================================

/**
 * @brief End of file marker
 */
struct __attribute__((packed)) EndRecord {
    uint8_t type;               // RecordType::End (0xFF)
    uint32_t totalRecords;      // Total records written
    uint32_t checksum;          // Simple checksum (reserved)
    
    static constexpr size_t SIZE = 9;
};

// ============================================================================
// File Footer (32 bytes) - Written on clean stop for integrity verification
// ============================================================================

/** @brief Footer magic number */
constexpr uint32_t FOOTER_MAGIC = 0xF007F007;

/**
 * @brief File footer for integrity verification
 * 
 * Written at end of file on clean stop. Absence indicates unclean shutdown.
 * Provides sample counts for data validation.
 */
struct __attribute__((packed)) FileFooter {
    uint32_t magic;             // FOOTER_MAGIC (0xF007F007)
    uint64_t totalAdcSamples;   // Total ADC samples written
    uint64_t totalImuSamples;   // Total IMU samples written
    uint32_t droppedSamples;    // Samples lost due to overflow
    uint32_t endTimestampUs;    // Final timestamp offset
    uint32_t crc32;             // CRC32 of all data (0 if not computed)
    
    // Size constant
    static constexpr size_t SIZE = 32;
    
    // Initialize with defaults
    void init() {
        magic = FOOTER_MAGIC;
        totalAdcSamples = 0;
        totalImuSamples = 0;
        droppedSamples = 0;
        endTimestampUs = 0;
        crc32 = 0;
    }
    
    // Validate footer
    bool isValid() const {
        return magic == FOOTER_MAGIC;
    }
};

static_assert(sizeof(FileFooter) == 32, "FileFooter must be 32 bytes");

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Calculate data rate in bytes per second
 * 
 * @param adcRateHz ADC sample rate
 * @param imuRateHz IMU sample rate
 * @param useTagged Whether using tagged records
 * @return Bytes per second
 */
inline uint32_t calculateDataRate(uint32_t adcRateHz, uint32_t imuRateHz, bool useTagged = false) {
    size_t adcSize = useTagged ? TaggedADCRecord::SIZE : ADCRecord::SIZE;
    size_t imuSize = useTagged ? TaggedIMURecord::SIZE : IMURecord::SIZE;
    return (adcRateHz * adcSize) + (imuRateHz * imuSize);
}

/**
 * @brief Estimate file size for given duration
 * 
 * @param adcRateHz ADC sample rate
 * @param imuRateHz IMU sample rate
 * @param durationSec Recording duration in seconds
 * @return Estimated file size in bytes
 */
inline uint64_t estimateFileSize(uint32_t adcRateHz, uint32_t imuRateHz, uint32_t durationSec) {
    return HEADER_SIZE + (uint64_t)calculateDataRate(adcRateHz, imuRateHz) * durationSec;
}

} // namespace BinaryFormat

#endif // BINARY_FORMAT_H

