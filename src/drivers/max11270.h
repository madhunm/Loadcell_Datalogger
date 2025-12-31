/**
 * @file max11270.h
 * @brief MAX11270 24-bit Delta-Sigma ADC Driver
 * 
 * High-performance driver for loadcell acquisition with:
 * - 24-bit resolution at up to 64 ksps
 * - DRDY interrupt-driven continuous mode
 * - Zero sample loss policy with overflow detection
 * - IRAM-optimized ISR for deterministic timing
 * 
 * Hardware Interface:
 *   MISO: GPIO 12 (SPI data from ADC)
 *   MOSI: GPIO 13 (SPI data to ADC)
 *   SCK:  GPIO 18 (SPI clock, 4 MHz)
 *   CS:   GPIO 17 (Chip select, active LOW)
 *   RDYB: GPIO 16 (Data ready interrupt, active LOW)
 *   RSTB: GPIO 15 (Hardware reset, active LOW)
 *   SYNC: GPIO 14 (Sync pulse)
 * 
 * Timing at 64 ksps:
 *   Sample interval: 15.625 µs
 *   SPI read time:   ~8 µs
 *   Headroom:        ~5 µs
 */

#ifndef MAX11270_H
#define MAX11270_H

#include <Arduino.h>
#include "../logging/ring_buffer.h"

namespace MAX11270 {

// ============================================================================
// Register Definitions (from MAX11270 datasheet)
// ============================================================================

/**
 * @brief MAX11270 register addresses
 */
enum class Register : uint8_t {
    STAT1   = 0x00,  ///< Status register 1
    CTRL1   = 0x01,  ///< Control register 1 (conversion mode, data rate)
    CTRL2   = 0x02,  ///< Control register 2 (PGA gain, GPIO)
    CTRL3   = 0x03,  ///< Control register 3 (sync, calibration)
    DATA    = 0x06,  ///< 24-bit conversion data
    SOC     = 0x07,  ///< System offset calibration
    SGC     = 0x09,  ///< System gain calibration
    SCOC    = 0x0B,  ///< Self-calibration offset
    SCGC    = 0x0D,  ///< Self-calibration gain
};

// ============================================================================
// Command Byte Definitions
// ============================================================================

// Command byte format: [START][MODE1][MODE0][CAL1][CAL0][LINEF][RATE3][RATE2][RATE1][RATE0]
// For continuous conversion: RATE selects output data rate

/**
 * @brief Conversion commands
 */
namespace Command {
    constexpr uint8_t POWERDOWN     = 0x00;  ///< Power down ADC
    constexpr uint8_t CONVERSION    = 0x80;  ///< Start conversion (single or continuous)
    constexpr uint8_t SEQUENCER     = 0xC0;  ///< Sequencer mode
    constexpr uint8_t CALIBRATE     = 0xA0;  ///< Calibration command
    
    // Read/Write register commands
    constexpr uint8_t READ_REG      = 0xC1;  ///< Read register (OR with reg address << 1)
    constexpr uint8_t WRITE_REG     = 0xC0;  ///< Write register (OR with reg address << 1)
}

// ============================================================================
// Configuration Enumerations
// ============================================================================

/**
 * @brief PGA (Programmable Gain Amplifier) settings
 * 
 * Higher gain = more sensitivity, less input range
 * For loadcells, 128x is typical for maximum sensitivity
 */
enum class Gain : uint8_t {
    X1   = 0x00,  ///< Gain = 1 (±2.5V input range)
    X2   = 0x01,  ///< Gain = 2 (±1.25V input range)
    X4   = 0x02,  ///< Gain = 4 (±625mV input range)
    X8   = 0x03,  ///< Gain = 8 (±312.5mV input range)
    X16  = 0x04,  ///< Gain = 16 (±156.25mV input range)
    X32  = 0x05,  ///< Gain = 32 (±78.125mV input range)
    X64  = 0x06,  ///< Gain = 64 (±39.0625mV input range)
    X128 = 0x07,  ///< Gain = 128 (±19.53125mV input range)
};

/**
 * @brief Sample rate settings
 * 
 * Higher rates = lower resolution (more noise)
 * 64 ksps is maximum for highest throughput
 */
enum class Rate : uint8_t {
    SPS_1_9   = 0x00,  ///<  1.9 sps  (highest resolution)
    SPS_3_9   = 0x01,  ///<  3.9 sps
    SPS_7_8   = 0x02,  ///<  7.8 sps
    SPS_15_6  = 0x03,  ///< 15.6 sps
    SPS_31_2  = 0x04,  ///< 31.2 sps
    SPS_62_5  = 0x05,  ///< 62.5 sps
    SPS_125   = 0x06,  ///< 125 sps
    SPS_250   = 0x07,  ///< 250 sps
    SPS_500   = 0x08,  ///< 500 sps
    SPS_1000  = 0x09,  ///< 1 ksps
    SPS_2000  = 0x0A,  ///< 2 ksps
    SPS_4000  = 0x0B,  ///< 4 ksps
    SPS_8000  = 0x0C,  ///< 8 ksps
    SPS_16000 = 0x0D,  ///< 16 ksps
    SPS_32000 = 0x0E,  ///< 32 ksps
    SPS_64000 = 0x0F,  ///< 64 ksps (maximum rate)
};

/**
 * @brief Conversion mode
 */
enum class Mode : uint8_t {
    Single     = 0x00,  ///< Single conversion, then idle
    Continuous = 0x01,  ///< Continuous conversions until stopped
};

// ============================================================================
// Status Flags
// ============================================================================

/**
 * @brief STAT1 register bit definitions
 */
namespace Status {
    constexpr uint8_t RDY       = 0x01;  ///< Data ready (conversion complete)
    constexpr uint8_t MSTAT     = 0x02;  ///< Modulator status
    constexpr uint8_t DOR       = 0x04;  ///< Data overrun (missed read)
    constexpr uint8_t SYSGOR    = 0x08;  ///< System gain overrange
    constexpr uint8_t RATE_MASK = 0xF0;  ///< Current data rate
}

// ============================================================================
// Configuration Structure
// ============================================================================

/**
 * @brief ADC configuration parameters
 */
struct Config {
    Gain gain = Gain::X128;       ///< PGA gain setting
    Rate rate = Rate::SPS_64000;  ///< Sample rate
    bool lineFilter = false;      ///< 50/60Hz line filter enable
    bool singleCycle = false;     ///< Single-cycle settling
};

// ============================================================================
// Statistics Structure
// ============================================================================

/**
 * @brief ADC acquisition statistics
 */
struct Statistics {
    uint32_t samplesAcquired;  ///< Total samples captured
    uint32_t samplesDropped;   ///< Samples lost (overflow)
    uint32_t drdyTimeouts;     ///< DRDY wait timeouts
    uint32_t spiErrors;        ///< SPI communication errors
    uint32_t dmaQueueFull;     ///< DMA queue full events
    uint32_t maxLatencyUs;     ///< Maximum ISR latency observed
    uint32_t lastTimestampUs;  ///< Timestamp of last sample
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize the MAX11270 ADC
 * 
 * Configures SPI, GPIO pins, and verifies communication.
 * Must be called before any other MAX11270 functions.
 * 
 * @return true if initialization successful, false if ADC not responding
 */
bool init();

/**
 * @brief Hardware reset the ADC
 * 
 * Pulses RSTB pin low to reset the ADC to default state.
 */
void reset();

/**
 * @brief Check if ADC is present and responding
 * 
 * Reads status register to verify SPI communication.
 * 
 * @return true if ADC responds correctly
 */
bool isPresent();

/**
 * @brief Configure PGA gain
 * 
 * @param gain Gain setting (1x to 128x)
 */
void setGain(Gain gain);

/**
 * @brief Get current PGA gain setting
 * 
 * @return Current gain
 */
Gain getGain();

/**
 * @brief Configure sample rate
 * 
 * @param rate Sample rate setting
 */
void setSampleRate(Rate rate);

/**
 * @brief Get current sample rate setting
 * 
 * @return Current rate
 */
Rate getSampleRate();

/**
 * @brief Apply full configuration
 * 
 * @param config Configuration structure
 */
void configure(const Config& config);

/**
 * @brief Perform a single conversion (blocking)
 * 
 * Starts a conversion and waits for completion.
 * Useful for testing and live preview.
 * 
 * @param timeout_ms Maximum time to wait for conversion
 * @return 24-bit ADC value (sign-extended to 32-bit), or INT32_MIN on error
 */
int32_t readSingle(uint32_t timeout_ms = 100);

/**
 * @brief Start continuous conversion mode
 * 
 * Enables DRDY interrupt-driven acquisition.
 * Samples are pushed to the provided ring buffer.
 * 
 * ZERO LOSS POLICY: If buffer becomes full, acquisition stops
 * and overflow flag is set rather than losing samples.
 * 
 * @param buffer Pointer to ring buffer for sample storage (128ms buffer)
 * @return true if continuous mode started successfully
 */
bool startContinuous(ADCRingBufferLarge* buffer);

/**
 * @brief Stop continuous conversion mode
 * 
 * Disables DRDY interrupt and stops ADC conversions.
 */
void stopContinuous();

/**
 * @brief Check if continuous mode is active
 * 
 * @return true if ADC is running in continuous mode
 */
bool isRunning();

/**
 * @brief Check if overflow has occurred
 * 
 * Non-zero indicates ZERO LOSS POLICY was violated!
 * Acquisition has been stopped to prevent further loss.
 * 
 * @return true if samples were lost
 */
bool hasOverflow();

/**
 * @brief Get overflow count
 * 
 * @return Number of samples lost due to buffer overflow
 */
uint32_t getOverflowCount();

/**
 * @brief Clear overflow flag
 * 
 * Call after handling/logging overflow condition.
 * Does not restart acquisition - call startContinuous() again.
 */
void clearOverflow();

/**
 * @brief Get acquisition statistics
 * 
 * @return Statistics structure with counts and timing info
 */
Statistics getStatistics();

/**
 * @brief Reset statistics counters
 */
void resetStatistics();

/**
 * @brief Read internal temperature sensor
 * 
 * MAX11270 has built-in temperature measurement.
 * 
 * @return Temperature in degrees Celsius
 */
float readTemperature();

/**
 * @brief Perform self-calibration
 * 
 * Runs internal offset and gain calibration.
 * Should be called after power-up or significant temperature change.
 * 
 * @return true if calibration completed successfully
 */
bool selfCalibrate();

// ============================================================================
// Low-Level Register Access (for diagnostics)
// ============================================================================

/**
 * @brief Read a register value
 * 
 * @param reg Register address
 * @return Register value (8, 16, or 24 bits depending on register)
 */
uint32_t readRegister(Register reg);

/**
 * @brief Write a register value
 * 
 * @param reg Register address
 * @param value Value to write
 */
void writeRegister(Register reg, uint32_t value);

/**
 * @brief Send raw command byte
 * 
 * @param cmd Command byte
 */
void sendCommand(uint8_t cmd);

// ============================================================================
// Conversion Helpers
// ============================================================================

/**
 * @brief Convert raw ADC value to microvolts
 * 
 * Uses current gain and reference voltage settings.
 * 
 * @param raw Raw 24-bit ADC value
 * @return Voltage in microvolts
 */
float rawToMicrovolts(int32_t raw);

/**
 * @brief Get sample rate in Hz
 * 
 * @param rate Rate enum value
 * @return Sample rate in Hz
 */
uint32_t rateToHz(Rate rate);

/**
 * @brief Get gain multiplier
 * 
 * @param gain Gain enum value
 * @return Gain multiplier (1-128)
 */
uint8_t gainToMultiplier(Gain gain);

} // namespace MAX11270

#endif // MAX11270_H

