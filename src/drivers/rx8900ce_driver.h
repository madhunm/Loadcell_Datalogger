/**
 * @file rx8900ce_driver.h
 * @brief I2C driver for RX8900CE Real-Time Clock
 */

#ifndef RX8900CE_DRIVER_H
#define RX8900CE_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include "../pin_config.h"

/** @brief RX8900CE register addresses */
#define RX8900_REG_SEC          0x00
#define RX8900_REG_MIN          0x01
#define RX8900_REG_HOUR         0x02
#define RX8900_REG_WEEK         0x03
#define RX8900_REG_DAY          0x04
#define RX8900_REG_MONTH        0x05
#define RX8900_REG_YEAR         0x06
#define RX8900_REG_EXT          0x0D
#define RX8900_REG_FLAG         0x0E
#define RX8900_REG_CTRL         0x0F

/**
 * @brief Date/time structure
 */
struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    
    /** @brief Convert to Unix timestamp (seconds since 1970-01-01) */
    uint32_t toUnixTime() const;
    
    /** @brief Set from Unix timestamp */
    void fromUnixTime(uint32_t t);
};

/**
 * @brief Driver for RX8900CE RTC with 1Hz sync output
 */
class RX8900CEDriver {
public:
    /**
     * @brief Initialize the RTC driver
     * @param wire Pointer to I2C interface (default: Wire)
     * @param addr I2C address (default: I2C_ADDR_RX8900CE)
     * @return true if successful, false otherwise
     */
    bool begin(TwoWire* wire = &Wire, uint8_t addr = I2C_ADDR_RX8900CE);
    
    /**
     * @brief Set the current date/time
     * @param dt DateTime structure
     * @return true if successful
     */
    bool setDateTime(const DateTime& dt);
    
    /**
     * @brief Get the current date/time
     * @param dt Output DateTime structure
     * @return true if successful
     */
    bool getDateTime(DateTime& dt);
    
    /**
     * @brief Enable 1Hz FOUT signal on GPIO pin
     * @return true if successful
     */
    bool enable1HzOutput();
    
    /**
     * @brief Disable FOUT signal
     * @return true if successful
     */
    bool disableFoutOutput();
    
    /**
     * @brief Enable update interrupt on INT pin
     * @return true if successful
     */
    bool enableUpdateInterrupt();
    
    /**
     * @brief Disable update interrupt
     * @return true if successful
     */
    bool disableUpdateInterrupt();
    
    /**
     * @brief Check if oscillator is running
     * @return true if running, false if stopped
     */
    bool isRunning();
    
    /**
     * @brief Get current Unix timestamp
     * @return Seconds since 1970-01-01, or 0 if error
     */
    uint32_t getUnixTime();
    
    /**
     * @brief Set time from Unix timestamp
     * @param timestamp Seconds since 1970-01-01
     * @return true if successful
     */
    bool setUnixTime(uint32_t timestamp);
    
private:
    TwoWire* wire;
    uint8_t i2c_addr;
    bool initialized;
    
    /**
     * @brief Write to a register
     */
    bool writeRegister(uint8_t reg, uint8_t value);
    
    /**
     * @brief Read from a register
     */
    bool readRegister(uint8_t reg, uint8_t& value);
    
    /**
     * @brief Read multiple registers
     */
    bool readRegisters(uint8_t reg, uint8_t* buffer, uint8_t len);
    
    /**
     * @brief Convert BCD to decimal
     */
    uint8_t bcdToDec(uint8_t bcd);
    
    /**
     * @brief Convert decimal to BCD
     */
    uint8_t decToBcd(uint8_t dec);
};

#endif // RX8900CE_DRIVER_H
