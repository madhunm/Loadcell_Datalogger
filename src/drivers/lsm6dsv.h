/**
 * @file lsm6dsv.h
 * @brief LSM6DSV 6-Axis IMU Driver with FIFO and DMA Support
 * 
 * Features:
 * - ESP-IDF I2C master driver with DMA for burst reads
 * - 3-axis accelerometer: ±2/4/8/16 g
 * - 3-axis gyroscope: ±125/250/500/1000/2000 dps
 * - Configurable ODR up to 7.68 kHz
 * - FIFO buffering up to 512 samples
 * - Watermark interrupt on INT1 for efficient DMA transfers
 */

#ifndef LSM6DSV_H
#define LSM6DSV_H

#include <Arduino.h>

namespace LSM6DSV {

// ============================================================================
// Device Identification
// ============================================================================

constexpr uint8_t WHO_AM_I_VALUE = 0x70;  // Expected WHO_AM_I response

// ============================================================================
// Register Definitions
// ============================================================================

namespace Reg {
    constexpr uint8_t FUNC_CFG_ACCESS   = 0x01;
    constexpr uint8_t PIN_CTRL_REG      = 0x02;  // Renamed to avoid conflict with ESP-IDF
    constexpr uint8_t IF_CFG            = 0x03;
    constexpr uint8_t FIFO_CTRL1        = 0x07;  // Watermark threshold [7:0]
    constexpr uint8_t FIFO_CTRL2        = 0x08;  // Watermark threshold [8], FIFO settings
    constexpr uint8_t FIFO_CTRL3        = 0x09;  // Batch data rates (accel/gyro)
    constexpr uint8_t FIFO_CTRL4        = 0x0A;  // FIFO mode selection
    constexpr uint8_t COUNTER_BDR_REG1  = 0x0B;
    constexpr uint8_t COUNTER_BDR_REG2  = 0x0C;
    constexpr uint8_t INT1_CTRL         = 0x0D;
    constexpr uint8_t INT2_CTRL         = 0x0E;
    constexpr uint8_t WHO_AM_I          = 0x0F;
    constexpr uint8_t CTRL1             = 0x10;  // Accelerometer ODR + FS
    constexpr uint8_t CTRL2             = 0x11;  // Gyroscope ODR + FS
    constexpr uint8_t CTRL3             = 0x12;  // Control register 3
    constexpr uint8_t CTRL4             = 0x13;
    constexpr uint8_t CTRL5             = 0x14;
    constexpr uint8_t CTRL6             = 0x15;
    constexpr uint8_t CTRL7             = 0x16;
    constexpr uint8_t CTRL8             = 0x17;
    constexpr uint8_t CTRL9             = 0x18;
    constexpr uint8_t CTRL10            = 0x19;
    constexpr uint8_t CTRL_STATUS       = 0x1A;
    constexpr uint8_t FIFO_STATUS1      = 0x1B;  // FIFO level [7:0]
    constexpr uint8_t FIFO_STATUS2      = 0x1C;  // FIFO level [8], status flags
    constexpr uint8_t ALL_INT_SRC       = 0x1D;
    constexpr uint8_t STATUS_REG        = 0x1E;
    
    // Output registers
    constexpr uint8_t OUT_TEMP_L        = 0x20;
    constexpr uint8_t OUT_TEMP_H        = 0x21;
    constexpr uint8_t OUTX_L_G          = 0x22;  // Gyro X low byte
    constexpr uint8_t OUTX_H_G          = 0x23;
    constexpr uint8_t OUTY_L_G          = 0x24;
    constexpr uint8_t OUTY_H_G          = 0x25;
    constexpr uint8_t OUTZ_L_G          = 0x26;
    constexpr uint8_t OUTZ_H_G          = 0x27;
    constexpr uint8_t OUTX_L_A          = 0x28;  // Accel X low byte
    constexpr uint8_t OUTX_H_A          = 0x29;
    constexpr uint8_t OUTY_L_A          = 0x2A;
    constexpr uint8_t OUTY_H_A          = 0x2B;
    constexpr uint8_t OUTZ_L_A          = 0x2C;
    constexpr uint8_t OUTZ_H_A          = 0x2D;
    
    // Timestamp
    constexpr uint8_t TIMESTAMP0        = 0x40;
    constexpr uint8_t TIMESTAMP1        = 0x41;
    constexpr uint8_t TIMESTAMP2        = 0x42;
    constexpr uint8_t TIMESTAMP3        = 0x43;
    
    // FIFO data output
    constexpr uint8_t FIFO_DATA_OUT_TAG = 0x78;  // Tag byte (identifies data type)
    constexpr uint8_t FIFO_DATA_OUT_X_L = 0x79;  // FIFO data output start
}

// ============================================================================
// Bit Definitions
// ============================================================================

namespace Bits {
    // CTRL3 register
    constexpr uint8_t SW_RESET      = 0x01;  // Software reset
    constexpr uint8_t IF_INC        = 0x04;  // Auto-increment address
    constexpr uint8_t BDU           = 0x40;  // Block data update
    constexpr uint8_t BOOT          = 0x80;  // Reboot memory content
    
    // INT1_CTRL register
    constexpr uint8_t INT1_DRDY_XL  = 0x01;  // Accel data ready on INT1
    constexpr uint8_t INT1_DRDY_G   = 0x02;  // Gyro data ready on INT1
    constexpr uint8_t INT1_FIFO_TH  = 0x08;  // FIFO threshold on INT1
    constexpr uint8_t INT1_FIFO_OVR = 0x10;  // FIFO overrun on INT1
    constexpr uint8_t INT1_FIFO_FULL= 0x20;  // FIFO full on INT1
    
    // STATUS_REG
    constexpr uint8_t XLDA          = 0x01;  // Accel data available
    constexpr uint8_t GDA           = 0x02;  // Gyro data available
    constexpr uint8_t TDA           = 0x04;  // Temperature data available
    
    // FIFO_STATUS2
    constexpr uint8_t FIFO_WTM_IA   = 0x80;  // FIFO watermark reached
    constexpr uint8_t FIFO_OVR_IA   = 0x40;  // FIFO overrun
    constexpr uint8_t FIFO_FULL_IA  = 0x20;  // FIFO full
    constexpr uint8_t FIFO_OVR_LATCHED = 0x08;  // Latched overrun flag
    
    // FIFO_CTRL4 - FIFO modes
    constexpr uint8_t FIFO_MODE_BYPASS      = 0x00;
    constexpr uint8_t FIFO_MODE_FIFO        = 0x01;  // Stop when full
    constexpr uint8_t FIFO_MODE_CONTINUOUS  = 0x06;  // Continuous (overwrite old)
    constexpr uint8_t FIFO_MODE_BYPASS_TO_FIFO = 0x07;
}

// ============================================================================
// Configuration Enums
// ============================================================================

/** @brief Accelerometer full-scale selection */
enum class AccelScale : uint8_t {
    G2   = 0x00,  // ±2 g
    G4   = 0x01,  // ±4 g
    G8   = 0x02,  // ±8 g
    G16  = 0x03   // ±16 g
};

/** @brief Gyroscope full-scale selection */
enum class GyroScale : uint8_t {
    DPS125  = 0x00,  // ±125 dps
    DPS250  = 0x01,  // ±250 dps
    DPS500  = 0x02,  // ±500 dps
    DPS1000 = 0x03,  // ±1000 dps
    DPS2000 = 0x04   // ±2000 dps
};

/** @brief Output data rate selection */
enum class ODR : uint8_t {
    PowerDown = 0x00,
    Hz1_875   = 0x01,  // 1.875 Hz
    Hz7_5     = 0x02,  // 7.5 Hz
    Hz15      = 0x03,  // 15 Hz
    Hz30      = 0x04,  // 30 Hz
    Hz60      = 0x05,  // 60 Hz
    Hz120     = 0x06,  // 120 Hz
    Hz240     = 0x07,  // 240 Hz
    Hz480     = 0x08,  // 480 Hz
    Hz960     = 0x09,  // 960 Hz
    Hz1920    = 0x0A,  // 1920 Hz
    Hz3840    = 0x0B,  // 3840 Hz
    Hz7680    = 0x0C   // 7680 Hz
};

/** @brief FIFO batch data rate for accel/gyro */
enum class FIFOBatchRate : uint8_t {
    NotBatched = 0x00,
    Hz1_875    = 0x01,
    Hz7_5      = 0x02,
    Hz15       = 0x03,
    Hz30       = 0x04,
    Hz60       = 0x05,
    Hz120      = 0x06,
    Hz240      = 0x07,
    Hz480      = 0x08,
    Hz960      = 0x09,
    Hz1920     = 0x0A,
    Hz3840     = 0x0B,
    Hz7680     = 0x0C
};

/** @brief FIFO operating mode */
enum class FIFOMode : uint8_t {
    Bypass          = 0x00,  // FIFO disabled
    FIFO            = 0x01,  // Stop when full
    Continuous      = 0x06,  // Continuous (overwrite oldest)
    BypassToFIFO    = 0x07   // Bypass until trigger, then FIFO
};

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Raw IMU data (12 bytes) */
struct RawData {
    int16_t accel[3];   // X, Y, Z accelerometer in raw counts
    int16_t gyro[3];    // X, Y, Z gyroscope in raw counts
};

/** @brief FIFO sample with tag (7 bytes from FIFO) */
struct FIFOSample {
    uint8_t tag;        // Data type tag
    int16_t data[3];    // X, Y, Z values
};

/** @brief Scaled IMU data */
struct ScaledData {
    float accel[3];     // X, Y, Z accelerometer in g
    float gyro[3];      // X, Y, Z gyroscope in dps
};

/** @brief Current configuration */
struct Config {
    ODR accelODR;
    ODR gyroODR;
    AccelScale accelScale;
    GyroScale gyroScale;
};

/** @brief FIFO configuration */
struct FIFOConfig {
    uint16_t watermark;         // Samples threshold (1-511)
    FIFOMode mode;              // Operating mode
    FIFOBatchRate accelBatchRate;
    FIFOBatchRate gyroBatchRate;
    bool enableTimestamp;       // Include timestamp in FIFO
};

/** @brief FIFO status */
struct FIFOStatus {
    uint16_t level;             // Current FIFO level (samples)
    bool watermarkReached;      // Watermark threshold exceeded
    bool overrun;               // FIFO overrun occurred
    bool full;                  // FIFO is full
};

/** @brief Statistics */
struct Statistics {
    uint32_t samplesRead;
    uint32_t fifoReads;
    uint32_t overruns;
    uint32_t dmaTransfers;
};

// ============================================================================
// FIFO Data Tags
// ============================================================================

namespace FIFOTag {
    constexpr uint8_t GYRO_NC       = 0x01;  // Gyroscope
    constexpr uint8_t ACCEL_NC      = 0x02;  // Accelerometer
    constexpr uint8_t TEMPERATURE   = 0x03;
    constexpr uint8_t TIMESTAMP     = 0x04;
    constexpr uint8_t CFG_CHANGE    = 0x05;
    constexpr uint8_t ACCEL_NC_T2   = 0x06;  // Accel at ODR/2
    constexpr uint8_t ACCEL_NC_T1   = 0x07;  // Accel at ODR/1
}

// ============================================================================
// Public API - Basic
// ============================================================================

/**
 * @brief Initialize the IMU driver with ESP-IDF I2C
 * @return true if initialization successful
 */
bool init();

/**
 * @brief Check if IMU is present
 * @return true if WHO_AM_I returns expected value
 */
bool isPresent();

/**
 * @brief Configure accelerometer
 */
bool configureAccel(ODR odr, AccelScale scale);

/**
 * @brief Configure gyroscope
 */
bool configureGyro(ODR odr, GyroScale scale);

/**
 * @brief Configure both sensors at once
 */
bool configure(ODR odr, AccelScale accelScale, GyroScale gyroScale);

/**
 * @brief Enable data-ready interrupt on INT1
 */
bool enableDataReadyInt(bool accel, bool gyro);

/**
 * @brief Check if new data is available
 */
bool dataReady(bool* accel = nullptr, bool* gyro = nullptr);

/**
 * @brief Read raw sensor data (blocking)
 */
bool readRaw(RawData* data);

/**
 * @brief Read and convert sensor data
 */
bool readScaled(ScaledData* data);

/**
 * @brief Read raw accelerometer only
 */
bool readAccelRaw(int16_t* x, int16_t* y, int16_t* z);

/**
 * @brief Read raw gyroscope only
 */
bool readGyroRaw(int16_t* x, int16_t* y, int16_t* z);

/**
 * @brief Read temperature
 */
float readTemperature();

/**
 * @brief Convert raw accelerometer value to g
 */
float rawToG(int16_t raw);

/**
 * @brief Convert raw gyroscope value to dps
 */
float rawToDPS(int16_t raw);

/**
 * @brief Get current configuration
 */
Config getConfig();

/**
 * @brief Software reset
 */
bool reset();

// ============================================================================
// Public API - FIFO
// ============================================================================

/**
 * @brief Configure FIFO
 * 
 * @param config FIFO configuration
 * @return true if successful
 */
bool configureFIFO(const FIFOConfig& config);

/**
 * @brief Enable FIFO with current configuration
 */
bool enableFIFO();

/**
 * @brief Disable FIFO (bypass mode)
 */
bool disableFIFO();

/**
 * @brief Enable FIFO watermark interrupt on INT1
 */
bool enableFIFOWatermarkInt();

/**
 * @brief Get FIFO status
 */
FIFOStatus getFIFOStatus();

/**
 * @brief Get number of samples in FIFO
 */
uint16_t getFIFOLevel();

/**
 * @brief Read samples from FIFO (blocking)
 * 
 * @param buffer Output buffer for RawData samples
 * @param maxSamples Maximum samples to read
 * @param actualSamples Output: actual samples read
 * @return true if successful
 */
bool readFIFO(RawData* buffer, uint16_t maxSamples, uint16_t* actualSamples);

/**
 * @brief Read raw FIFO data with tags
 * 
 * @param buffer Output buffer for FIFOSample
 * @param maxSamples Maximum samples to read
 * @param actualSamples Output: actual samples read
 * @return true if successful
 */
bool readFIFORaw(FIFOSample* buffer, uint16_t maxSamples, uint16_t* actualSamples);

/**
 * @brief Flush FIFO (discard all data)
 */
bool flushFIFO();

// ============================================================================
// Public API - DMA (Non-blocking)
// ============================================================================

/**
 * @brief Start async DMA FIFO read
 * 
 * @param buffer DMA-capable buffer (must remain valid until complete)
 * @param sampleCount Number of samples to read
 * @return true if DMA transfer started
 */
bool startFIFORead_DMA(FIFOSample* buffer, uint16_t sampleCount);

/**
 * @brief Check if DMA transfer is complete
 */
bool isFIFOReadComplete();

/**
 * @brief Wait for DMA transfer to complete
 * @param timeoutMs Timeout in milliseconds
 * @return true if completed, false if timeout
 */
bool waitFIFOReadComplete(uint32_t timeoutMs = 100);

/**
 * @brief Get the number of samples from last DMA read
 */
uint16_t getLastDMAReadCount();

// ============================================================================
// Public API - Statistics
// ============================================================================

/**
 * @brief Get driver statistics
 */
Statistics getStatistics();

/**
 * @brief Reset statistics
 */
void resetStatistics();

// ============================================================================
// Public API - Low Level
// ============================================================================

/**
 * @brief Read raw register value
 */
bool readRegister(uint8_t reg, uint8_t* value);

/**
 * @brief Write raw register value
 */
bool writeRegister(uint8_t reg, uint8_t value);

/**
 * @brief Read multiple registers
 */
bool readRegisters(uint8_t startReg, uint8_t* data, size_t len);

} // namespace LSM6DSV

#endif // LSM6DSV_H
