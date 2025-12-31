/**
 * @file lsm6dsv_driver.cpp
 * @brief Implementation of LSM6DSV 6-axis IMU driver
 */

#include "lsm6dsv_driver.h"

bool LSM6DSVDriver::writeRegister(uint8_t reg, uint8_t value) {
    wire->beginTransmission(i2c_addr);
    wire->write(reg);
    wire->write(value);
    return (wire->endTransmission() == 0);
}

bool LSM6DSVDriver::readRegister(uint8_t reg, uint8_t& value) {
    wire->beginTransmission(i2c_addr);
    wire->write(reg);
    if (wire->endTransmission(false) != 0) {
        return false;
    }
    
    if (wire->requestFrom(i2c_addr, (uint8_t)1) != 1) {
        return false;
    }
    
    value = wire->read();
    return true;
}

bool LSM6DSVDriver::readRegisters(uint8_t reg, uint8_t* buffer, uint8_t len) {
    wire->beginTransmission(i2c_addr);
    wire->write(reg);
    if (wire->endTransmission(false) != 0) {
        return false;
    }
    
    if (wire->requestFrom(i2c_addr, len) != len) {
        return false;
    }
    
    for (uint8_t i = 0; i < len; i++) {
        buffer[i] = wire->read();
    }
    
    return true;
}

bool LSM6DSVDriver::begin(TwoWire* wire_obj, uint8_t addr) {
    wire = wire_obj;
    i2c_addr = addr;
    initialized = false;
    
    // Check WHO_AM_I register
    uint8_t who_am_i;
    if (!readRegister(LSM6DSV_WHO_AM_I, who_am_i)) {
        return false;
    }
    
    if (who_am_i != LSM6DSV_ID) {
        return false;
    }
    
    // Reset device (CTRL3_C bit 0)
    if (!writeRegister(LSM6DSV_CTRL3_C, 0x01)) {
        return false;
    }
    delay(10);
    
    // Default configuration: 1kHz for both sensors
    accel_scale = ACCEL_16G;  // High range for dynamic measurements
    gyro_scale = GYRO_2000_DPS;
    
    if (!configAccel(ACCEL_ODR_1666_HZ, accel_scale)) {
        return false;
    }
    
    if (!configGyro(GYRO_ODR_1666_HZ, gyro_scale)) {
        return false;
    }
    
    initialized = true;
    return true;
}

bool LSM6DSVDriver::configAccel(AccelODR odr, AccelScale scale) {
    // CTRL1_XL: ODR in bits 7:4, FS in bits 3:2
    uint8_t ctrl1 = ((odr & 0x0F) << 4) | ((scale & 0x03) << 2);
    
    if (!writeRegister(LSM6DSV_CTRL1_XL, ctrl1)) {
        return false;
    }
    
    accel_scale = scale;
    return true;
}

bool LSM6DSVDriver::configGyro(GyroODR odr, GyroScale scale) {
    // CTRL2_G: ODR in bits 7:4, FS in bits 3:1
    uint8_t ctrl2 = ((odr & 0x0F) << 4) | ((scale & 0x07) << 1);
    
    if (!writeRegister(LSM6DSV_CTRL2_G, ctrl2)) {
        return false;
    }
    
    gyro_scale = scale;
    return true;
}

bool LSM6DSVDriver::dataAvailable() {
    uint8_t status;
    if (!readRegister(LSM6DSV_STATUS_REG, status)) {
        return false;
    }
    
    // Bit 0: Accel data available
    // Bit 1: Gyro data available
    return (status & 0x03) == 0x03;
}

bool IRAM_ATTR LSM6DSVDriver::readDataFast(IMUSample& sample) {
    uint8_t buffer[12];
    
    // Read gyro (6 bytes) and accel (6 bytes) in one transaction
    if (!readRegisters(LSM6DSV_OUTX_L_G, buffer, 12)) {
        return false;
    }
    
    // Parse gyro data (bytes 0-5)
    sample.gyro_x = (int16_t)(buffer[1] << 8 | buffer[0]);
    sample.gyro_y = (int16_t)(buffer[3] << 8 | buffer[2]);
    sample.gyro_z = (int16_t)(buffer[5] << 8 | buffer[4]);
    
    // Parse accel data (bytes 6-11)
    sample.accel_x = (int16_t)(buffer[7] << 8 | buffer[6]);
    sample.accel_y = (int16_t)(buffer[9] << 8 | buffer[8]);
    sample.accel_z = (int16_t)(buffer[11] << 8 | buffer[10]);
    
    return true;
}

bool LSM6DSVDriver::readData(IMUSample& sample) {
    sample.timestamp_offset_us = 0;  // Caller should set this
    return readDataFast(sample);
}

float LSM6DSVDriver::accelToMps2(int16_t raw) {
    float sensitivity;
    
    switch (accel_scale) {
        case ACCEL_2G:  sensitivity = 0.061f;  break;  // mg/LSB
        case ACCEL_4G:  sensitivity = 0.122f;  break;
        case ACCEL_8G:  sensitivity = 0.244f;  break;
        case ACCEL_16G: sensitivity = 0.488f;  break;
        default: sensitivity = 0.061f; break;
    }
    
    // Convert to m/s^2 (1g = 9.80665 m/s^2)
    return (raw * sensitivity / 1000.0f) * 9.80665f;
}

float LSM6DSVDriver::gyroToDps(int16_t raw) {
    float sensitivity;
    
    switch (gyro_scale) {
        case GYRO_125_DPS:  sensitivity = 4.375f;  break;  // mdps/LSB
        case GYRO_250_DPS:  sensitivity = 8.75f;   break;
        case GYRO_500_DPS:  sensitivity = 17.50f;  break;
        case GYRO_1000_DPS: sensitivity = 35.0f;   break;
        case GYRO_2000_DPS: sensitivity = 70.0f;   break;
        default: sensitivity = 70.0f; break;
    }
    
    // Convert to degrees per second
    return raw * sensitivity / 1000.0f;
}
