/**
 * @file max17048.cpp
 * @brief MAX17048 Fuel Gauge Driver Implementation
 * 
 * Uses Arduino Wire library for I2C communication (shared bus with RTC and IMU).
 */

#include "max17048.h"
#include <Wire.h>
#include <esp_log.h>

static const char* TAG = "MAX17048";

namespace MAX17048 {

namespace {
    TwoWire* wire = nullptr;
    bool initialized = false;
    
    // I2C helpers
    bool i2cRead(uint8_t reg, uint8_t* data, size_t len) {
        if (!wire) return false;
        
        wire->beginTransmission(I2C_ADDRESS);
        wire->write(reg);
        if (wire->endTransmission(false) != 0) {
            return false;
        }
        
        size_t received = wire->requestFrom((uint8_t)I2C_ADDRESS, (uint8_t)len);
        if (received != len) {
            return false;
        }
        
        for (size_t i = 0; i < len; i++) {
            data[i] = wire->read();
        }
        
        return true;
    }
    
    bool i2cWrite(uint8_t reg, const uint8_t* data, size_t len) {
        if (!wire) return false;
        
        wire->beginTransmission(I2C_ADDRESS);
        wire->write(reg);
        for (size_t i = 0; i < len; i++) {
            wire->write(data[i]);
        }
        return wire->endTransmission() == 0;
    }
}

// ============================================================================
// Initialization
// ============================================================================

bool init() {
    wire = &Wire;  // Use shared Wire instance
    
    if (!isPresent()) {
        ESP_LOGE(TAG, "Device not found at 0x%02X", I2C_ADDRESS);
        return false;
    }
    
    // Read and verify version
    uint16_t version = getVersion();
    ESP_LOGI(TAG, "Found MAX17048/9, version: 0x%04X", version);
    
    // Clear any pending alerts
    clearAlerts();
    
    initialized = true;
    return true;
}

bool isPresent() {
    if (!wire) wire = &Wire;
    
    wire->beginTransmission(I2C_ADDRESS);
    return wire->endTransmission() == 0;
}

uint16_t getVersion() {
    uint16_t version = 0;
    readRegister(Reg::VERSION, &version);
    return version;
}

// ============================================================================
// Battery Measurements
// ============================================================================

float getVoltage() {
    uint16_t raw = 0;
    if (!readRegister(Reg::VCELL, &raw)) {
        return -1.0f;
    }
    
    // VCELL is 12-bit value in upper bits, 78.125µV per bit
    // Formula: voltage = raw * 78.125µV = raw * 78.125 / 1000000 V
    // Simplified: voltage = raw * 0.000078125
    // Or: voltage = raw / 12800
    return (float)raw * 78.125f / 1000000.0f;
}

float getSOC() {
    uint16_t raw = 0;
    if (!readRegister(Reg::SOC, &raw)) {
        return -1.0f;
    }
    
    // SOC register: MSB = integer %, LSB = 1/256 %
    // Formula: SOC = MSB + LSB/256
    return (float)(raw >> 8) + (float)(raw & 0xFF) / 256.0f;
}

float getChargeRate() {
    uint16_t raw = 0;
    if (!readRegister(Reg::CRATE, &raw)) {
        return 0.0f;
    }
    
    // CRATE is signed 16-bit value, 0.208%/hr per bit
    int16_t signedRaw = (int16_t)raw;
    return (float)signedRaw * 0.208f;
}

bool getBatteryData(BatteryData* data) {
    if (!data) return false;
    
    data->voltage = getVoltage();
    data->socPercent = getSOC();
    data->chargeRate = getChargeRate();
    data->statusFlags = getStatus();
    data->alertActive = (data->statusFlags & 0x3E) != 0;  // Any alert bits set
    
    return data->voltage >= 0 && data->socPercent >= 0;
}

// ============================================================================
// Status and Alerts
// ============================================================================

uint8_t getStatus() {
    uint16_t raw = 0;
    if (!readRegister(Reg::STATUS, &raw)) {
        return 0xFF;
    }
    return (uint8_t)(raw >> 8);  // Status is in MSB
}

void clearAlerts() {
    // Read current status
    uint16_t status = 0;
    if (readRegister(Reg::STATUS, &status)) {
        // Clear alert bits by writing 0 to them (write 1 to RI to clear reset indicator)
        status &= 0x0100;  // Keep only RI bit to clear it
        writeRegister(Reg::STATUS, status);
    }
}

// ============================================================================
// Power Management
// ============================================================================

void quickStart() {
    // Write 0x4000 to MODE register to trigger quick-start
    writeRegister(Reg::MODE, 0x4000);
    ESP_LOGI(TAG, "Quick-start initiated");
}

void sleep() {
    // Read current config
    uint16_t config = 0;
    if (readRegister(Reg::CONFIG, &config)) {
        // Set SLEEP bit (bit 7 of LSB)
        config |= 0x0080;
        writeRegister(Reg::CONFIG, config);
        ESP_LOGI(TAG, "Entering sleep mode");
    }
}

void wake() {
    // Read current config
    uint16_t config = 0;
    if (readRegister(Reg::CONFIG, &config)) {
        // Clear SLEEP bit
        config &= ~0x0080;
        writeRegister(Reg::CONFIG, config);
        ESP_LOGI(TAG, "Waking from sleep");
    }
}

// ============================================================================
// Alert Configuration
// ============================================================================

void setVoltageAlert(float minV, float maxV) {
    // VALRT register: MSB = max threshold, LSB = min threshold
    // Each bit = 20mV, range 0-5.1V
    uint8_t minThresh = (uint8_t)(minV / 0.020f);
    uint8_t maxThresh = (uint8_t)(maxV / 0.020f);
    
    uint16_t valrt = ((uint16_t)maxThresh << 8) | minThresh;
    writeRegister(Reg::VALRT, valrt);
    
    ESP_LOGI(TAG, "Voltage alert set: %.2fV - %.2fV", minV, maxV);
}

void setSOCAlert(uint8_t percent) {
    // SOC alert threshold is in CONFIG register bits 4:0
    if (percent > 32) percent = 32;
    
    uint16_t config = 0;
    if (readRegister(Reg::CONFIG, &config)) {
        config = (config & 0xFFE0) | (32 - percent);  // Threshold is inverted
        writeRegister(Reg::CONFIG, config);
        ESP_LOGI(TAG, "SOC alert set at %d%%", percent);
    }
}

// ============================================================================
// Low-level Register Access
// ============================================================================

bool readRegister(uint8_t reg, uint16_t* value) {
    uint8_t data[2];
    if (!i2cRead(reg, data, 2)) {
        return false;
    }
    
    // MAX17048 is big-endian
    *value = ((uint16_t)data[0] << 8) | data[1];
    return true;
}

bool writeRegister(uint8_t reg, uint16_t value) {
    uint8_t data[2] = {
        (uint8_t)(value >> 8),    // MSB first
        (uint8_t)(value & 0xFF)   // LSB
    };
    return i2cWrite(reg, data, 2);
}

} // namespace MAX17048

