/**
 * @file max11270_driver.h
 * @brief SPI driver for MAX11270 24-bit ADC
 */

#ifndef MAX11270_DRIVER_H
#define MAX11270_DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include "../pin_config.h"

/** @brief MAX11270 register addresses */
#define MAX11270_REG_STAT1      0x00
#define MAX11270_REG_CTRL1      0x01
#define MAX11270_REG_CTRL2      0x02
#define MAX11270_REG_CTRL3      0x03
#define MAX11270_REG_DATA       0x04
#define MAX11270_REG_SOC        0x05
#define MAX11270_REG_SGC        0x06
#define MAX11270_REG_SCOC       0x07
#define MAX11270_REG_SCGC       0x08

/** @brief Command bytes */
#define MAX11270_CMD_CONVERSION 0x80
#define MAX11270_CMD_CAL_SELF   0x82
#define MAX11270_CMD_CAL_PGA    0x84

/**
 * @brief Driver for MAX11270 24-bit delta-sigma ADC
 */
class MAX11270Driver {
public:
    /**
     * @brief ADC sample rate configuration
     */
    enum SampleRate {
        RATE_1_9_SPS = 0,
        RATE_3_9_SPS,
        RATE_7_8_SPS,
        RATE_15_6_SPS,
        RATE_31_2_SPS,
        RATE_62_5_SPS,
        RATE_125_SPS,
        RATE_250_SPS,
        RATE_500_SPS,
        RATE_1000_SPS,
        RATE_2000_SPS,
        RATE_4000_SPS,
        RATE_8000_SPS,
        RATE_16000_SPS,
        RATE_32000_SPS,
        RATE_64000_SPS  // Target rate for this application
    };
    
    /**
     * @brief PGA gain settings
     */
    enum Gain {
        GAIN_1X = 0,
        GAIN_2X,
        GAIN_4X,
        GAIN_8X,
        GAIN_16X,
        GAIN_32X,
        GAIN_64X,
        GAIN_128X
    };
    
    /**
     * @brief Initialize the MAX11270 driver
     * @return true if successful, false otherwise
     */
    bool begin();
    
    /**
     * @brief Configure ADC sample rate
     * @param rate Desired sample rate
     * @return true if successful
     */
    bool setSampleRate(SampleRate rate);
    
    /**
     * @brief Configure PGA gain
     * @param gain Desired gain setting
     * @return true if successful
     */
    bool setGain(Gain gain);
    
    /**
     * @brief Start continuous conversion mode
     * @return true if successful
     */
    bool startContinuous();
    
    /**
     * @brief Stop continuous conversion mode
     * @return true if successful
     */
    bool stopContinuous();
    
    /**
     * @brief Read raw 24-bit ADC value (blocking)
     * @param value Output: raw ADC value (sign-extended to int32)
     * @return true if successful
     */
    bool readRaw(int32_t& value);
    
    /**
     * @brief Read raw ADC value (non-blocking, for ISR use)
     * Must only be called when RDYB is LOW
     * @return Raw 24-bit value (sign-extended to int32)
     */
    int32_t IRAM_ATTR readRawFast();
    
    /**
     * @brief Convert raw ADC value to microvolts
     * @param raw_value Raw 24-bit ADC reading
     * @param ref_voltage Reference voltage in volts (typically 2.5V)
     * @return Value in microvolts
     */
    float rawToMicrovolts(int32_t raw_value, float ref_voltage = 2.5f);
    
    /**
     * @brief Perform self-calibration
     * @return true if successful
     */
    bool performSelfCalibration();
    
    /**
     * @brief Check if data is ready (RDYB pin state)
     * @return true if data ready (RDYB LOW)
     */
    bool isDataReady();
    
    /**
     * @brief Get current configuration
     */
    SampleRate getCurrentRate() const { return current_rate; }
    Gain getCurrentGain() const { return current_gain; }
    
private:
    SPIClass* spi;
    SampleRate current_rate;
    Gain current_gain;
    bool initialized;
    
    /**
     * @brief Write to a register
     */
    bool writeRegister(uint8_t reg, uint32_t value, uint8_t bytes = 1);
    
    /**
     * @brief Read from a register
     */
    bool readRegister(uint8_t reg, uint32_t& value, uint8_t bytes = 3);
    
    /**
     * @brief Send a command
     */
    bool sendCommand(uint8_t cmd);
    
    /**
     * @brief Reset the ADC
     */
    void reset();
    
    /**
     * @brief Wait for conversion ready
     */
    bool waitForReady(uint32_t timeout_ms = 1000);
};

#endif // MAX11270_DRIVER_H
