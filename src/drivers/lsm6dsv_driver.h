/**
 * @file lsm6dsv_driver.h
 * @brief I2C driver for LSM6DSV 6-axis IMU (accelerometer + gyroscope)
 */

#ifndef LSM6DSV_DRIVER_H
#define LSM6DSV_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include "../pin_config.h"

/** @brief LSM6DSV register addresses */
#define LSM6DSV_WHO_AM_I        0x0F
#define LSM6DSV_CTRL1_XL        0x10
#define LSM6DSV_CTRL2_G         0x11
#define LSM6DSV_CTRL3_C         0x12
#define LSM6DSV_STATUS_REG      0x1E
#define LSM6DSV_OUT_TEMP_L      0x20
#define LSM6DSV_OUTX_L_G        0x22
#define LSM6DSV_OUTX_L_A        0x28

/** @brief WHO_AM_I value for LSM6DSV */
#define LSM6DSV_ID              0x70

/**
 * @brief IMU data structure
 */
struct IMUSample {
    uint32_t timestamp_offset_us;  ///< Timestamp offset from start
    int16_t accel_x;               ///< X-axis acceleration (raw)
    int16_t accel_y;               ///< Y-axis acceleration (raw)
    int16_t accel_z;               ///< Z-axis acceleration (raw)
    int16_t gyro_x;                ///< X-axis gyroscope (raw)
    int16_t gyro_y;                ///< Y-axis gyroscope (raw)
    int16_t gyro_z;                ///< Z-axis gyroscope (raw)
};

/**
 * @brief Driver for LSM6DSV 6-axis IMU
 */
class LSM6DSVDriver {
public:
    /**
     * @brief Accelerometer output data rate
     */
    enum AccelODR {
        ACCEL_ODR_OFF = 0,
        ACCEL_ODR_12_5_HZ,
        ACCEL_ODR_26_HZ,
        ACCEL_ODR_52_HZ,
        ACCEL_ODR_104_HZ,
        ACCEL_ODR_208_HZ,
        ACCEL_ODR_416_HZ,
        ACCEL_ODR_833_HZ,
        ACCEL_ODR_1666_HZ,
        ACCEL_ODR_3333_HZ,
        ACCEL_ODR_6666_HZ
    };
    
    /**
     * @brief Gyroscope output data rate
     */
    enum GyroODR {
        GYRO_ODR_OFF = 0,
        GYRO_ODR_12_5_HZ,
        GYRO_ODR_26_HZ,
        GYRO_ODR_52_HZ,
        GYRO_ODR_104_HZ,
        GYRO_ODR_208_HZ,
        GYRO_ODR_416_HZ,
        GYRO_ODR_833_HZ,
        GYRO_ODR_1666_HZ,
        GYRO_ODR_3333_HZ,
        GYRO_ODR_6666_HZ
    };
    
    /**
     * @brief Accelerometer full scale range
     */
    enum AccelScale {
        ACCEL_2G = 0,
        ACCEL_4G,
        ACCEL_8G,
        ACCEL_16G
    };
    
    /**
     * @brief Gyroscope full scale range
     */
    enum GyroScale {
        GYRO_125_DPS = 0,
        GYRO_250_DPS,
        GYRO_500_DPS,
        GYRO_1000_DPS,
        GYRO_2000_DPS
    };
    
    /**
     * @brief Initialize the LSM6DSV driver
     * @param wire Pointer to I2C interface (default: Wire)
     * @param addr I2C address (default: I2C_ADDR_LSM6DSV)
     * @return true if successful, false otherwise
     */
    bool begin(TwoWire* wire = &Wire, uint8_t addr = I2C_ADDR_LSM6DSV);
    
    /**
     * @brief Configure accelerometer
     * @param odr Output data rate
     * @param scale Full scale range
     * @return true if successful
     */
    bool configAccel(AccelODR odr, AccelScale scale);
    
    /**
     * @brief Configure gyroscope
     * @param odr Output data rate
     * @param scale Full scale range
     * @return true if successful
     */
    bool configGyro(GyroODR odr, GyroScale scale);
    
    /**
     * @brief Read accelerometer and gyroscope data
     * @param sample Output structure for IMU data
     * @return true if successful
     */
    bool readData(IMUSample& sample);
    
    /**
     * @brief Fast read for ISR context (no timestamp)
     * @param sample Output structure
     * @return true if successful
     */
    bool IRAM_ATTR readDataFast(IMUSample& sample);
    
    /**
     * @brief Convert raw accelerometer value to m/s^2
     * @param raw Raw 16-bit value
     * @return Acceleration in m/s^2
     */
    float accelToMps2(int16_t raw);
    
    /**
     * @brief Convert raw gyroscope value to degrees/sec
     * @param raw Raw 16-bit value
     * @return Angular velocity in degrees/sec
     */
    float gyroToDps(int16_t raw);
    
    /**
     * @brief Check if new data is available
     * @return true if both accel and gyro have new data
     */
    bool dataAvailable();
    
private:
    TwoWire* wire;
    uint8_t i2c_addr;
    AccelScale accel_scale;
    GyroScale gyro_scale;
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
     * @brief Read multiple bytes
     */
    bool readRegisters(uint8_t reg, uint8_t* buffer, uint8_t len);
};

#endif // LSM6DSV_DRIVER_H
