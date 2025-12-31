/**
 * @file max11270_driver.cpp
 * @brief Implementation of MAX11270 24-bit ADC driver
 */

#include "max11270_driver.h"

void MAX11270Driver::reset() {
    // Hardware reset using RSTB pin
    digitalWrite(PIN_ADC_RSTB, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_ADC_RSTB, HIGH);
    delay(100); // Wait for power-up
}

bool MAX11270Driver::begin() {
    // Initialize SPI pins
    pinMode(PIN_ADC_CS, OUTPUT);
    pinMode(PIN_ADC_RSTB, OUTPUT);
    pinMode(PIN_ADC_SYNC, OUTPUT);
    pinMode(PIN_ADC_RDYB, INPUT);
    
    digitalWrite(PIN_ADC_CS, HIGH);
    digitalWrite(PIN_ADC_RSTB, HIGH);
    digitalWrite(PIN_ADC_SYNC, HIGH);
    
    // Initialize SPI
    spi = new SPIClass(HSPI);
    spi->begin(PIN_ADC_SCK, PIN_ADC_MISO, PIN_ADC_MOSI, PIN_ADC_CS);
    
    // Reset ADC
    reset();
    
    // Set default configuration
    current_rate = RATE_64000_SPS;
    current_gain = GAIN_1X;
    
    // Configure for continuous conversion at 64ksps
    if (!setSampleRate(RATE_64000_SPS)) {
        return false;
    }
    
    if (!setGain(GAIN_1X)) {
        return false;
    }
    
    initialized = true;
    return true;
}

bool MAX11270Driver::writeRegister(uint8_t reg, uint32_t value, uint8_t bytes) {
    spi->beginTransaction(SPISettings(ADC_SPI_FREQ_HZ, MSBFIRST, ADC_SPI_MODE));
    digitalWrite(PIN_ADC_CS, LOW);
    
    // Write register address with write bit (bit 7 = 0 for write)
    spi->transfer((reg << 1) & 0xFE);
    
    // Write data bytes (MSB first)
    for (int i = bytes - 1; i >= 0; i--) {
        spi->transfer((value >> (i * 8)) & 0xFF);
    }
    
    digitalWrite(PIN_ADC_CS, HIGH);
    spi->endTransaction();
    
    return true;
}

bool MAX11270Driver::readRegister(uint8_t reg, uint32_t& value, uint8_t bytes) {
    spi->beginTransaction(SPISettings(ADC_SPI_FREQ_HZ, MSBFIRST, ADC_SPI_MODE));
    digitalWrite(PIN_ADC_CS, LOW);
    
    // Write register address with read bit (bit 7 = 1 for read)
    spi->transfer((reg << 1) | 0x01);
    
    // Read data bytes (MSB first)
    value = 0;
    for (int i = bytes - 1; i >= 0; i--) {
        value |= ((uint32_t)spi->transfer(0x00) << (i * 8));
    }
    
    digitalWrite(PIN_ADC_CS, HIGH);
    spi->endTransaction();
    
    return true;
}

bool MAX11270Driver::sendCommand(uint8_t cmd) {
    spi->beginTransaction(SPISettings(ADC_SPI_FREQ_HZ, MSBFIRST, ADC_SPI_MODE));
    digitalWrite(PIN_ADC_CS, LOW);
    
    spi->transfer(cmd);
    
    digitalWrite(PIN_ADC_CS, HIGH);
    spi->endTransaction();
    
    return true;
}

bool MAX11270Driver::setSampleRate(SampleRate rate) {
    // CTRL3 register controls sample rate
    // Lower 4 bits select rate
    uint32_t ctrl3 = rate & 0x0F;
    
    if (!writeRegister(MAX11270_REG_CTRL3, ctrl3, 1)) {
        return false;
    }
    
    current_rate = rate;
    return true;
}

bool MAX11270Driver::setGain(Gain gain) {
    // CTRL2 register controls PGA gain
    // Bits 0-2 select gain
    uint32_t ctrl2 = gain & 0x07;
    
    if (!writeRegister(MAX11270_REG_CTRL2, ctrl2, 1)) {
        return false;
    }
    
    current_gain = gain;
    return true;
}

bool MAX11270Driver::startContinuous() {
    // CTRL1: Set continuous conversion mode
    // Bit 4 = 1 for continuous conversion
    uint32_t ctrl1 = (1 << 4);
    
    if (!writeRegister(MAX11270_REG_CTRL1, ctrl1, 1)) {
        return false;
    }
    
    // Start conversion command
    return sendCommand(MAX11270_CMD_CONVERSION);
}

bool MAX11270Driver::stopContinuous() {
    // CTRL1: Clear continuous conversion mode
    uint32_t ctrl1 = 0;
    return writeRegister(MAX11270_REG_CTRL1, ctrl1, 1);
}

bool MAX11270Driver::isDataReady() {
    return digitalRead(PIN_ADC_RDYB) == LOW;
}

bool MAX11270Driver::waitForReady(uint32_t timeout_ms) {
    uint32_t start = millis();
    while (!isDataReady()) {
        if (millis() - start > timeout_ms) {
            return false;
        }
        yield();
    }
    return true;
}

bool MAX11270Driver::readRaw(int32_t& value) {
    if (!waitForReady()) {
        return false;
    }
    
    value = readRawFast();
    return true;
}

int32_t IRAM_ATTR MAX11270Driver::readRawFast() {
    uint32_t raw24 = 0;
    
    spi->beginTransaction(SPISettings(ADC_SPI_FREQ_HZ, MSBFIRST, ADC_SPI_MODE));
    digitalWrite(PIN_ADC_CS, LOW);
    
    // Read data register address
    spi->transfer((MAX11270_REG_DATA << 1) | 0x01);
    
    // Read 24-bit data (MSB first)
    raw24 = ((uint32_t)spi->transfer(0x00) << 16) |
            ((uint32_t)spi->transfer(0x00) << 8) |
             (uint32_t)spi->transfer(0x00);
    
    digitalWrite(PIN_ADC_CS, HIGH);
    spi->endTransaction();
    
    // Sign-extend 24-bit to 32-bit
    if (raw24 & 0x800000) {
        return (int32_t)(raw24 | 0xFF000000);
    } else {
        return (int32_t)raw24;
    }
}

float MAX11270Driver::rawToMicrovolts(int32_t raw_value, float ref_voltage) {
    // 24-bit ADC: -2^23 to +2^23-1
    // Full scale range: +/-ref_voltage / gain
    
    float full_scale = ref_voltage;
    
    // Apply gain divisor
    uint8_t gain_value = 1 << current_gain; // 1, 2, 4, 8, 16, 32, 64, 128
    full_scale /= gain_value;
    
    // Convert to voltage
    float voltage = (raw_value / 8388608.0f) * full_scale;
    
    // Convert to microvolts
    return voltage * 1000000.0f;
}

bool MAX11270Driver::performSelfCalibration() {
    // Send self-calibration command
    if (!sendCommand(MAX11270_CMD_CAL_SELF)) {
        return false;
    }
    
    // Wait for calibration to complete (can take several conversion periods)
    delay(500);
    
    // Check STAT1 register for calibration done
    uint32_t stat1;
    if (!readRegister(MAX11270_REG_STAT1, stat1, 1)) {
        return false;
    }
    
    // Bit 0 should be 0 when calibration is complete
    return (stat1 & 0x01) == 0;
}
