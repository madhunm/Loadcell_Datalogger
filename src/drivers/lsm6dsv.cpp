/**
 * @file lsm6dsv.cpp
 * @brief LSM6DSV 6-Axis IMU Driver with FIFO Support
 * 
 * Uses Arduino Wire library for I2C communication (shared bus with RTC).
 */

#include "lsm6dsv.h"
#include "../pin_config.h"
#include <Wire.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <atomic>

static const char* TAG = "LSM6DSV";

namespace LSM6DSV {

namespace {
    // I2C instance (shared with RTC)
    TwoWire* wire = nullptr;
    bool initialized = false;
    uint8_t deviceAddr = I2C_ADDR_LSM6DSV;
    
    // Current configuration
    Config currentConfig = {
        .accelODR = ODR::PowerDown,
        .gyroODR = ODR::PowerDown,
        .accelScale = AccelScale::G2,
        .gyroScale = GyroScale::DPS250
    };
    
    // FIFO configuration
    FIFOConfig currentFIFOConfig = {
        .watermark = 64,
        .mode = FIFOMode::Bypass,
        .accelBatchRate = FIFOBatchRate::NotBatched,
        .gyroBatchRate = FIFOBatchRate::NotBatched,
        .enableTimestamp = false
    };
    
    // Statistics
    std::atomic<uint32_t> statSamplesRead{0};
    std::atomic<uint32_t> statFIFOReads{0};
    std::atomic<uint32_t> statOverruns{0};
    std::atomic<uint32_t> statDMATransfers{0};
    
    // Sensitivity lookup
    float getAccelSensitivity(AccelScale scale) {
        switch (scale) {
            case AccelScale::G2:  return 0.061f;   // mg/LSB
            case AccelScale::G4:  return 0.122f;
            case AccelScale::G8:  return 0.244f;
            case AccelScale::G16: return 0.488f;
            default: return 0.061f;
        }
    }
    
    float getGyroSensitivity(GyroScale scale) {
        switch (scale) {
            case GyroScale::DPS125:  return 4.375f;    // mdps/LSB
            case GyroScale::DPS250:  return 8.75f;
            case GyroScale::DPS500:  return 17.5f;
            case GyroScale::DPS1000: return 35.0f;
            case GyroScale::DPS2000: return 70.0f;
            default: return 8.75f;
        }
    }
    
    // I2C read helper (Arduino Wire)
    bool i2cRead(uint8_t reg, uint8_t* data, size_t len) {
        if (!wire) return false;
        
        wire->beginTransmission(deviceAddr);
        wire->write(reg);
        if (wire->endTransmission(false) != 0) {
            return false;
        }
        
        size_t received = wire->requestFrom(deviceAddr, (uint8_t)len);
        if (received != len) {
            return false;
        }
        
        for (size_t i = 0; i < len; i++) {
            data[i] = wire->read();
        }
        
        return true;
    }
    
    // I2C write helper (Arduino Wire)
    bool i2cWrite(uint8_t reg, const uint8_t* data, size_t len) {
        if (!wire) return false;
        
        wire->beginTransmission(deviceAddr);
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
    // Use the default Wire instance (shared with RTC)
    // Note: Wire must be initialized by caller (scanI2C in main.cpp)
    wire = &Wire;
    
    // Try primary address first
    deviceAddr = I2C_ADDR_LSM6DSV;
    if (!isPresent()) {
        // Try alternate address
        deviceAddr = I2C_ADDR_LSM6DSV_ALT;
        if (!isPresent()) {
            ESP_LOGE(TAG, "Device not found at 0x%02X or 0x%02X", 
                     I2C_ADDR_LSM6DSV, I2C_ADDR_LSM6DSV_ALT);
            return false;
        }
        ESP_LOGI(TAG, "Found at alternate address 0x%02X", deviceAddr);
    }
    
    // Check WHO_AM_I value
    uint8_t whoAmI;
    if (!readRegister(Reg::WHO_AM_I, &whoAmI)) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I");
        return false;
    }
    
    if (whoAmI != WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: got 0x%02X, expected 0x%02X", whoAmI, WHO_AM_I_VALUE);
        return false;
    }
    
    // Software reset
    if (!reset()) {
        ESP_LOGE(TAG, "Reset failed");
        return false;
    }
    
    delay(10);
    
    // Configure CTRL3: BDU=1, IF_INC=1
    uint8_t ctrl3 = Bits::BDU | Bits::IF_INC;
    if (!writeRegister(Reg::CTRL3, ctrl3)) {
        ESP_LOGE(TAG, "CTRL3 config failed");
        return false;
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Initialized at 0x%02X (WHO_AM_I=0x%02X)", deviceAddr, whoAmI);
    return true;
}

bool isPresent() {
    if (!wire) return false;
    
    wire->beginTransmission(deviceAddr);
    return wire->endTransmission() == 0;
}

// ============================================================================
// Configuration
// ============================================================================

bool configureAccel(ODR odr, AccelScale scale) {
    if (!initialized) return false;
    
    // LSM6DSV16X CTRL1 register format:
    // Bits 3:0 = ODR_XL[3:0] - Output data rate
    // Bits 6:4 = OP_MODE_XL[2:0] - Operating mode (0=high-performance)
    // Bit 7 = (reserved)
    uint8_t ctrl1 = static_cast<uint8_t>(odr) & 0x0F;  // ODR in low nibble
    
    ESP_LOGI(TAG, "Accel config: ODR=0x%X, Scale=0x%X, CTRL1=0x%02X", 
             static_cast<uint8_t>(odr), static_cast<uint8_t>(scale), ctrl1);
    
    if (!writeRegister(Reg::CTRL1, ctrl1)) {
        ESP_LOGE(TAG, "Failed to write CTRL1!");
        return false;
    }
    
    // LSM6DSV16X: Full scale is in CTRL8 bits[1:0]
    uint8_t ctrl8 = static_cast<uint8_t>(scale) & 0x03;
    if (!writeRegister(Reg::CTRL8, ctrl8)) {
        ESP_LOGE(TAG, "Failed to write CTRL8!");
        return false;
    }
    
    // Small delay for configuration to take effect
    delay(5);
    
    // Verify the writes
    uint8_t readBack1 = 0, readBack8 = 0;
    readRegister(Reg::CTRL1, &readBack1);
    readRegister(Reg::CTRL8, &readBack8);
    ESP_LOGI(TAG, "Accel verify: CTRL1=0x%02X (ODR=%d), CTRL8=0x%02X (FS=%d)", 
             readBack1, readBack1 & 0x0F, readBack8, readBack8 & 0x03);
    
    currentConfig.accelODR = odr;
    currentConfig.accelScale = scale;
    return true;
}

bool configureGyro(ODR odr, GyroScale scale) {
    if (!initialized) return false;
    
    // LSM6DSV16X CTRL2 register format:
    // Bits 3:0 = ODR_G[3:0] - Output data rate
    // Bits 6:4 = OP_MODE_G[2:0] - Operating mode (0=high-performance)
    // Bit 7 = (reserved)
    uint8_t ctrl2 = static_cast<uint8_t>(odr) & 0x0F;  // ODR in low nibble
    
    if (!writeRegister(Reg::CTRL2, ctrl2)) {
        ESP_LOGE(TAG, "Failed to write CTRL2!");
        return false;
    }
    
    // LSM6DSV16X: Gyro full scale is in CTRL6 bits[3:0]
    uint8_t ctrl6 = static_cast<uint8_t>(scale) & 0x0F;
    if (!writeRegister(Reg::CTRL6, ctrl6)) {
        ESP_LOGE(TAG, "Failed to write CTRL6!");
        return false;
    }
    
    ESP_LOGI(TAG, "Gyro config: CTRL2=0x%02X (ODR=%d), CTRL6=0x%02X (FS=%d)",
             ctrl2, ctrl2 & 0x0F, ctrl6, ctrl6 & 0x0F);
    
    currentConfig.gyroODR = odr;
    currentConfig.gyroScale = scale;
    return true;
}

bool configure(ODR odr, AccelScale accelScale, GyroScale gyroScale) {
    bool ok = configureAccel(odr, accelScale);
    ok &= configureGyro(odr, gyroScale);
    
    // Verify registers were written correctly
    uint8_t ctrl1 = 0, ctrl2 = 0, ctrl3 = 0, ctrl6 = 0, ctrl8 = 0;
    readRegister(Reg::CTRL1, &ctrl1);
    readRegister(Reg::CTRL2, &ctrl2);
    readRegister(Reg::CTRL3, &ctrl3);
    readRegister(Reg::CTRL6, &ctrl6);
    readRegister(Reg::CTRL8, &ctrl8);
    
    ESP_LOGI(TAG, "Config verify: CTRL1=0x%02X, CTRL2=0x%02X, CTRL3=0x%02X, CTRL6=0x%02X, CTRL8=0x%02X", 
             ctrl1, ctrl2, ctrl3, ctrl6, ctrl8);
    
    // LSM6DSV16X: ODR is in bits[3:0] of CTRL1/CTRL2
    uint8_t accelOdrBits = ctrl1 & 0x0F;
    uint8_t gyroOdrBits = ctrl2 & 0x0F;
    if (accelOdrBits == 0) {
        ESP_LOGW(TAG, "WARNING: Accel ODR=0 (power down) - check CTRL1 write!");
    }
    if (gyroOdrBits == 0) {
        ESP_LOGW(TAG, "WARNING: Gyro ODR=0 (power down) - check CTRL2 write!");
    }
    
    if (ok) {
        ESP_LOGI(TAG, "Configured: ODR=%d, Accel=%dg, Gyro=%ddps",
                 static_cast<int>(odr),
                 2 << static_cast<int>(accelScale),
                 125 << static_cast<int>(gyroScale));
    }
    return ok;
}

bool enableDataReadyInt(bool accel, bool gyro) {
    if (!initialized) return false;
    
    uint8_t int1Ctrl = 0;
    if (accel) int1Ctrl |= Bits::INT1_DRDY_XL;
    if (gyro) int1Ctrl |= Bits::INT1_DRDY_G;
    
    return writeRegister(Reg::INT1_CTRL, int1Ctrl);
}

// ============================================================================
// Basic Data Reading
// ============================================================================

bool dataReady(bool* accel, bool* gyro) {
    uint8_t status;
    if (!readRegister(Reg::STATUS_REG, &status)) {
        return false;
    }
    
    bool accelReady = (status & Bits::XLDA) != 0;
    bool gyroReady = (status & Bits::GDA) != 0;
    
    if (accel) *accel = accelReady;
    if (gyro) *gyro = gyroReady;
    
    return accelReady || gyroReady;
}

bool readRaw(RawData* data) {
    if (!data || !initialized) return false;
    
    uint8_t buf[12];
    if (!readRegisters(Reg::OUTX_L_G, buf, 12)) {
        return false;
    }
    
    // Gyroscope (first 6 bytes)
    data->gyro[0] = (int16_t)(buf[1] << 8 | buf[0]);
    data->gyro[1] = (int16_t)(buf[3] << 8 | buf[2]);
    data->gyro[2] = (int16_t)(buf[5] << 8 | buf[4]);
    
    // Accelerometer (next 6 bytes)
    data->accel[0] = (int16_t)(buf[7] << 8 | buf[6]);
    data->accel[1] = (int16_t)(buf[9] << 8 | buf[8]);
    data->accel[2] = (int16_t)(buf[11] << 8 | buf[10]);
    
    statSamplesRead++;
    return true;
}

bool readScaled(ScaledData* data) {
    if (!data) return false;
    
    RawData raw;
    if (!readRaw(&raw)) {
        return false;
    }
    
    float accelSens = getAccelSensitivity(currentConfig.accelScale);
    data->accel[0] = raw.accel[0] * accelSens / 1000.0f;
    data->accel[1] = raw.accel[1] * accelSens / 1000.0f;
    data->accel[2] = raw.accel[2] * accelSens / 1000.0f;
    
    float gyroSens = getGyroSensitivity(currentConfig.gyroScale);
    data->gyro[0] = raw.gyro[0] * gyroSens / 1000.0f;
    data->gyro[1] = raw.gyro[1] * gyroSens / 1000.0f;
    data->gyro[2] = raw.gyro[2] * gyroSens / 1000.0f;
    
    return true;
}

bool readAccelRaw(int16_t* x, int16_t* y, int16_t* z) {
    if (!initialized) return false;
    
    uint8_t buf[6];
    if (!readRegisters(Reg::OUTX_L_A, buf, 6)) {
        return false;
    }
    
    if (x) *x = (int16_t)(buf[1] << 8 | buf[0]);
    if (y) *y = (int16_t)(buf[3] << 8 | buf[2]);
    if (z) *z = (int16_t)(buf[5] << 8 | buf[4]);
    
    return true;
}

bool readGyroRaw(int16_t* x, int16_t* y, int16_t* z) {
    if (!initialized) return false;
    
    uint8_t buf[6];
    if (!readRegisters(Reg::OUTX_L_G, buf, 6)) {
        return false;
    }
    
    if (x) *x = (int16_t)(buf[1] << 8 | buf[0]);
    if (y) *y = (int16_t)(buf[3] << 8 | buf[2]);
    if (z) *z = (int16_t)(buf[5] << 8 | buf[4]);
    
    return true;
}

float readTemperature() {
    if (!initialized) return -273.15f;
    
    uint8_t buf[2];
    if (!readRegisters(Reg::OUT_TEMP_L, buf, 2)) {
        return -273.15f;
    }
    
    int16_t raw = (int16_t)(buf[1] << 8 | buf[0]);
    return 25.0f + (float)raw / 256.0f;
}

float rawToG(int16_t raw) {
    float sens = getAccelSensitivity(currentConfig.accelScale);
    return raw * sens / 1000.0f;
}

float rawToDPS(int16_t raw) {
    float sens = getGyroSensitivity(currentConfig.gyroScale);
    return raw * sens / 1000.0f;
}

Config getConfig() {
    return currentConfig;
}

bool reset() {
    uint8_t ctrl3 = Bits::SW_RESET;
    if (!writeRegister(Reg::CTRL3, ctrl3)) {
        return false;
    }
    
    delay(10);
    
    for (int i = 0; i < 10; i++) {
        if (readRegister(Reg::CTRL3, &ctrl3)) {
            if (!(ctrl3 & Bits::SW_RESET)) {
                return true;
            }
        }
        delay(1);
    }
    
    return false;
}

// ============================================================================
// FIFO Configuration
// ============================================================================

bool configureFIFO(const FIFOConfig& config) {
    if (!initialized) return false;
    
    currentFIFOConfig = config;
    
    // FIFO_CTRL1: Watermark threshold [7:0]
    uint8_t fifoCtrl1 = config.watermark & 0xFF;
    if (!writeRegister(Reg::FIFO_CTRL1, fifoCtrl1)) return false;
    
    // FIFO_CTRL2: Watermark threshold [8], stop on WTM
    uint8_t fifoCtrl2 = (config.watermark >> 8) & 0x01;
    if (!writeRegister(Reg::FIFO_CTRL2, fifoCtrl2)) return false;
    
    // FIFO_CTRL3: Batch data rates
    uint8_t fifoCtrl3 = (static_cast<uint8_t>(config.gyroBatchRate) << 4) |
                        static_cast<uint8_t>(config.accelBatchRate);
    if (!writeRegister(Reg::FIFO_CTRL3, fifoCtrl3)) return false;
    
    // FIFO_CTRL4: FIFO mode
    uint8_t fifoCtrl4 = static_cast<uint8_t>(config.mode);
    if (config.enableTimestamp) {
        fifoCtrl4 |= 0x40;
    }
    if (!writeRegister(Reg::FIFO_CTRL4, fifoCtrl4)) return false;
    
    ESP_LOGI(TAG, "FIFO configured: WTM=%d, Mode=%d", config.watermark, static_cast<int>(config.mode));
    return true;
}

bool enableFIFO() {
    if (!initialized) return false;
    
    currentFIFOConfig.mode = FIFOMode::Continuous;
    uint8_t fifoCtrl4 = static_cast<uint8_t>(FIFOMode::Continuous);
    return writeRegister(Reg::FIFO_CTRL4, fifoCtrl4);
}

bool disableFIFO() {
    if (!initialized) return false;
    
    currentFIFOConfig.mode = FIFOMode::Bypass;
    return writeRegister(Reg::FIFO_CTRL4, Bits::FIFO_MODE_BYPASS);
}

bool enableFIFOWatermarkInt() {
    if (!initialized) return false;
    return writeRegister(Reg::INT1_CTRL, Bits::INT1_FIFO_TH);
}

FIFOStatus getFIFOStatus() {
    FIFOStatus status = {0};
    
    uint8_t buf[2];
    if (!readRegisters(Reg::FIFO_STATUS1, buf, 2)) {
        return status;
    }
    
    status.level = buf[0] | ((buf[1] & 0x03) << 8);
    status.watermarkReached = (buf[1] & Bits::FIFO_WTM_IA) != 0;
    status.overrun = (buf[1] & Bits::FIFO_OVR_IA) != 0;
    status.full = (buf[1] & Bits::FIFO_FULL_IA) != 0;
    
    if (status.overrun) {
        statOverruns++;
    }
    
    return status;
}

uint16_t getFIFOLevel() {
    uint8_t buf[2];
    if (!readRegisters(Reg::FIFO_STATUS1, buf, 2)) {
        return 0;
    }
    return buf[0] | ((buf[1] & 0x03) << 8);
}

// ============================================================================
// FIFO Reading
// ============================================================================

bool readFIFO(RawData* buffer, uint16_t maxSamples, uint16_t* actualSamples) {
    if (!buffer || !actualSamples || !initialized) return false;
    
    uint16_t level = getFIFOLevel();
    if (level == 0) {
        *actualSamples = 0;
        return true;
    }
    
    uint16_t toRead = (level < maxSamples) ? level : maxSamples;
    
    // Read FIFO samples (7 bytes each: tag + 6 data bytes)
    RawData currentSample = {0};
    bool haveAccel = false;
    bool haveGyro = false;
    uint16_t outputIndex = 0;
    
    for (uint16_t i = 0; i < toRead && outputIndex < maxSamples; i++) {
        uint8_t fifoData[7];
        if (!readRegisters(Reg::FIFO_DATA_OUT_TAG, fifoData, 7)) {
            break;
        }
        
        uint8_t tag = fifoData[0] >> 3;
        int16_t x = (int16_t)(fifoData[2] << 8 | fifoData[1]);
        int16_t y = (int16_t)(fifoData[4] << 8 | fifoData[3]);
        int16_t z = (int16_t)(fifoData[6] << 8 | fifoData[5]);
        
        if (tag == FIFOTag::GYRO_NC) {
            currentSample.gyro[0] = x;
            currentSample.gyro[1] = y;
            currentSample.gyro[2] = z;
            haveGyro = true;
        } else if (tag == FIFOTag::ACCEL_NC) {
            currentSample.accel[0] = x;
            currentSample.accel[1] = y;
            currentSample.accel[2] = z;
            haveAccel = true;
        }
        
        if (haveAccel && haveGyro) {
            buffer[outputIndex++] = currentSample;
            haveAccel = false;
            haveGyro = false;
            memset(&currentSample, 0, sizeof(currentSample));
        }
    }
    
    *actualSamples = outputIndex;
    statSamplesRead += outputIndex;
    statFIFOReads++;
    
    return true;
}

bool readFIFORaw(FIFOSample* buffer, uint16_t maxSamples, uint16_t* actualSamples) {
    if (!buffer || !actualSamples || !initialized) return false;
    
    uint16_t level = getFIFOLevel();
    if (level == 0) {
        *actualSamples = 0;
        return true;
    }
    
    uint16_t toRead = (level < maxSamples) ? level : maxSamples;
    
    for (uint16_t i = 0; i < toRead; i++) {
        uint8_t fifoData[7];
        if (!readRegisters(Reg::FIFO_DATA_OUT_TAG, fifoData, 7)) {
            *actualSamples = i;
            return false;
        }
        
        buffer[i].tag = fifoData[0] >> 3;
        buffer[i].data[0] = (int16_t)(fifoData[2] << 8 | fifoData[1]);
        buffer[i].data[1] = (int16_t)(fifoData[4] << 8 | fifoData[3]);
        buffer[i].data[2] = (int16_t)(fifoData[6] << 8 | fifoData[5]);
    }
    
    *actualSamples = toRead;
    statFIFOReads++;
    return true;
}

bool flushFIFO() {
    FIFOMode savedMode = currentFIFOConfig.mode;
    
    if (!writeRegister(Reg::FIFO_CTRL4, Bits::FIFO_MODE_BYPASS)) {
        return false;
    }
    
    delay(1);
    
    if (savedMode != FIFOMode::Bypass) {
        return writeRegister(Reg::FIFO_CTRL4, static_cast<uint8_t>(savedMode));
    }
    
    return true;
}

// ============================================================================
// DMA Operations (simplified for Wire - blocking reads)
// ============================================================================

bool startFIFORead_DMA(FIFOSample* buffer, uint16_t sampleCount) {
    // Wire doesn't support true DMA, so this is a blocking burst read
    uint16_t actualRead;
    bool ok = readFIFORaw(buffer, sampleCount, &actualRead);
    if (ok) {
        statDMATransfers++;
    }
    return ok;
}

bool isFIFOReadComplete() {
    return true;  // Wire reads are always blocking/complete
}

bool waitFIFOReadComplete(uint32_t timeoutMs) {
    (void)timeoutMs;
    return true;
}

uint16_t getLastDMAReadCount() {
    return 0;  // Not tracked in Wire mode
}

// ============================================================================
// Statistics
// ============================================================================

Statistics getStatistics() {
    return {
        .samplesRead = statSamplesRead.load(),
        .fifoReads = statFIFOReads.load(),
        .overruns = statOverruns.load(),
        .dmaTransfers = statDMATransfers.load()
    };
}

void resetStatistics() {
    statSamplesRead = 0;
    statFIFOReads = 0;
    statOverruns = 0;
    statDMATransfers = 0;
}

// ============================================================================
// Low Level
// ============================================================================

bool readRegister(uint8_t reg, uint8_t* value) {
    if (!value) return false;
    return i2cRead(reg, value, 1);
}

bool writeRegister(uint8_t reg, uint8_t value) {
    return i2cWrite(reg, &value, 1);
}

bool readRegisters(uint8_t startReg, uint8_t* data, size_t len) {
    if (!data) return false;
    return i2cRead(startReg, data, len);
}

} // namespace LSM6DSV
