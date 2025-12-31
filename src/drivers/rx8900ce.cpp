/**
 * @file rx8900ce.cpp
 * @brief RX8900CE Real-Time Clock Driver Implementation
 */

#include "rx8900ce.h"
#include "../pin_config.h"
#include <Wire.h>

namespace RX8900CE {

namespace {
    // I2C instance
    TwoWire* wire = nullptr;
    bool initialized = false;
    
    // BCD conversion helpers
    uint8_t bcdToDec(uint8_t bcd) {
        return ((bcd >> 4) * 10) + (bcd & 0x0F);
    }
    
    uint8_t decToBcd(uint8_t dec) {
        return ((dec / 10) << 4) | (dec % 10);
    }
    
    // Read multiple registers
    bool readRegisters(uint8_t startReg, uint8_t* data, size_t len) {
        if (!wire) return false;
        
        wire->beginTransmission(I2C_ADDR_RX8900CE);
        wire->write(startReg);
        if (wire->endTransmission(false) != 0) {
            return false;
        }
        
        size_t received = wire->requestFrom((uint8_t)I2C_ADDR_RX8900CE, (uint8_t)len);
        if (received != len) {
            return false;
        }
        
        for (size_t i = 0; i < len; i++) {
            data[i] = wire->read();
        }
        
        return true;
    }
    
    // Write multiple registers
    bool writeRegisters(uint8_t startReg, const uint8_t* data, size_t len) {
        if (!wire) return false;
        
        wire->beginTransmission(I2C_ADDR_RX8900CE);
        wire->write(startReg);
        for (size_t i = 0; i < len; i++) {
            wire->write(data[i]);
        }
        return wire->endTransmission() == 0;
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool init() {
    // Use the default Wire instance (must be initialized by caller via scanI2C or Wire.begin)
    wire = &Wire;
    
    // Note: We do NOT call Wire.begin() here because it may already be initialized
    // by the I2C scan in main.cpp. Calling it again can cause issues.
    
    // Check if device is present
    if (!isPresent()) {
        Serial.println("[RX8900] Device not found at 0x32");
        return false;
    }
    
    // Check VLF flag (voltage low - indicates data may be invalid)
    uint8_t flagReg;
    if (readRegister(Reg::FLAG, &flagReg)) {
        if (flagReg & Bits::VLF) {
            Serial.println("[RX8900] Warning: VLF set - time data may be invalid");
            // Clear the flag
            writeRegister(Reg::FLAG, flagReg & ~(Bits::VLF | Bits::VDET));
        }
    }
    
    initialized = true;
    Serial.println("[RX8900] Initialized");
    return true;
}

bool isPresent() {
    if (!wire) return false;
    
    wire->beginTransmission(I2C_ADDR_RX8900CE);
    return wire->endTransmission() == 0;
}

bool isTimeValid() {
    uint8_t flagReg;
    if (!readRegister(Reg::FLAG, &flagReg)) {
        return false;
    }
    // Time is valid if VLF (voltage low flag) is NOT set
    return !(flagReg & Bits::VLF);
}

bool getTime(struct tm* t) {
    if (!t || !initialized) return false;
    
    uint8_t data[7];
    if (!readRegisters(Reg::SEC, data, 7)) {
        return false;
    }
    
    t->tm_sec  = bcdToDec(data[0] & 0x7F);
    t->tm_min  = bcdToDec(data[1] & 0x7F);
    t->tm_hour = bcdToDec(data[2] & 0x3F);
    t->tm_wday = data[3] & 0x07;  // Day of week (0=Sunday)
    t->tm_mday = bcdToDec(data[4] & 0x3F);
    t->tm_mon  = bcdToDec(data[5] & 0x1F) - 1;  // struct tm months are 0-11
    t->tm_year = bcdToDec(data[6]) + 100;  // struct tm years since 1900, RTC years since 2000
    t->tm_isdst = 0;
    
    return true;
}

bool setTime(const struct tm* t) {
    if (!t || !initialized) return false;
    
    uint8_t data[7];
    data[0] = decToBcd(t->tm_sec);
    data[1] = decToBcd(t->tm_min);
    data[2] = decToBcd(t->tm_hour);
    data[3] = t->tm_wday & 0x07;
    data[4] = decToBcd(t->tm_mday);
    data[5] = decToBcd(t->tm_mon + 1);  // Convert from 0-11 to 1-12
    data[6] = decToBcd(t->tm_year - 100);  // Convert from years since 1900 to years since 2000
    
    if (!writeRegisters(Reg::SEC, data, 7)) {
        return false;
    }
    
    // Clear VLF flag after setting time
    uint8_t flagReg;
    if (readRegister(Reg::FLAG, &flagReg)) {
        writeRegister(Reg::FLAG, flagReg & ~(Bits::VLF | Bits::VDET));
    }
    
    return true;
}

time_t getEpoch() {
    struct tm t;
    if (!getTime(&t)) {
        return 0;
    }
    return mktime(&t);
}

bool setEpoch(time_t epoch) {
    struct tm* t = gmtime(&epoch);
    if (!t) return false;
    return setTime(t);
}

bool setFOUT(FOUTFreq freq) {
    if (!initialized) return false;
    
    uint8_t extReg;
    if (!readRegister(Reg::EXTENSION, &extReg)) {
        return false;
    }
    
    // Clear FSEL bits and set new frequency
    extReg &= ~(Bits::FSEL0 | Bits::FSEL1);
    extReg |= static_cast<uint8_t>(freq);
    
    return writeRegister(Reg::EXTENSION, extReg);
}

bool enableFOUT1Hz() {
    bool result = setFOUT(FOUTFreq::Hz1);
    if (result) {
        // Verify configuration
        uint8_t extReg;
        if (readRegister(Reg::EXTENSION, &extReg)) {
            uint8_t fsel = extReg & (Bits::FSEL0 | Bits::FSEL1);
            Serial.printf("[RX8900] FOUT configured: EXT=0x%02X, FSEL=0x%02X (expected 0x08 for 1Hz)\n", 
                          extReg, fsel);
            if (fsel != 0x08) {
                Serial.println("[RX8900] WARNING: FSEL not set to 1Hz!");
                return false;
            }
        }
        Serial.println("[RX8900] FOUT 1Hz enabled successfully");
    } else {
        Serial.println("[RX8900] FOUT 1Hz enable FAILED!");
    }
    return result;
}

bool disableFOUT() {
    return setFOUT(FOUTFreq::Off);
}

float getTemperature() {
    if (!initialized) return -128.0f;
    
    uint8_t tempReg;
    if (!readRegister(Reg::TEMP, &tempReg)) {
        return -128.0f;
    }
    
    // Empirical calibration: raw=153 (0x99) at 25°C room temperature
    // Formula: temp = (raw - 103) * 0.5
    // This gives: (153 - 103) * 0.5 = 25°C
    float temp = (tempReg - 103) * 0.5f;
    
    // Debug output (can be removed once verified)
    Serial.printf("[RX8900] TEMP raw=0x%02X (%d), calc=%.1f°C\n", 
                  tempReg, tempReg, temp);
    
    return temp;
}

bool clearFlags() {
    return writeRegister(Reg::FLAG, 0x00);
}

bool readRegister(uint8_t reg, uint8_t* value) {
    if (!wire || !value) return false;
    
    wire->beginTransmission(I2C_ADDR_RX8900CE);
    wire->write(reg);
    if (wire->endTransmission(false) != 0) {
        return false;
    }
    
    if (wire->requestFrom((uint8_t)I2C_ADDR_RX8900CE, (uint8_t)1) != 1) {
        return false;
    }
    
    *value = wire->read();
    return true;
}

bool writeRegister(uint8_t reg, uint8_t value) {
    if (!wire) return false;
    
    wire->beginTransmission(I2C_ADDR_RX8900CE);
    wire->write(reg);
    wire->write(value);
    return wire->endTransmission() == 0;
}

// ============================================================================
// Compile-Time Sync Functions
// ============================================================================

namespace {
    // Month name to number mapping
    int monthNameToNumber(const char* name) {
        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        for (int i = 0; i < 12; i++) {
            if (strncmp(name, months[i], 3) == 0) {
                return i;  // 0-indexed for struct tm
            }
        }
        return -1;
    }
}

time_t getCompileEpoch() {
    // __DATE__ format: "Dec 31 2025"
    // __TIME__ format: "14:30:00"
    const char* compileDate = __DATE__;
    const char* compileTime = __TIME__;
    
    struct tm t;
    memset(&t, 0, sizeof(t));
    
    // Parse month (first 3 chars)
    char monthStr[4] = {0};
    strncpy(monthStr, compileDate, 3);
    t.tm_mon = monthNameToNumber(monthStr);
    if (t.tm_mon < 0) {
        Serial.printf("[RX8900] Failed to parse month: %s\n", monthStr);
        return 0;
    }
    
    // Parse day (chars 4-5, may have leading space)
    t.tm_mday = atoi(compileDate + 4);
    
    // Parse year (chars 7-10)
    t.tm_year = atoi(compileDate + 7) - 1900;  // years since 1900
    
    // Parse time HH:MM:SS
    t.tm_hour = atoi(compileTime);
    t.tm_min = atoi(compileTime + 3);
    t.tm_sec = atoi(compileTime + 6);
    
    t.tm_isdst = 0;
    
    return mktime(&t);
}

bool syncToCompileTime() {
    time_t compileTime = getCompileEpoch();
    if (compileTime == 0) {
        Serial.println("[RX8900] Failed to get compile time");
        return false;
    }
    
    char timeBuf[24];
    formatTime(compileTime, timeBuf, sizeof(timeBuf));
    Serial.printf("[RX8900] Syncing to compile time: %s\n", timeBuf);
    
    if (setEpoch(compileTime)) {
        Serial.println("[RX8900] Time synced successfully");
        return true;
    }
    
    Serial.println("[RX8900] Time sync FAILED");
    return false;
}

bool needsTimeSync() {
    // Check VLF flag
    uint8_t flagReg;
    if (readRegister(Reg::FLAG, &flagReg)) {
        if (flagReg & Bits::VLF) {
            Serial.println("[RX8900] VLF flag set - time may be invalid");
            return true;
        }
    }
    
    // Get current RTC time
    struct tm rtcTime;
    if (!getTime(&rtcTime)) {
        Serial.println("[RX8900] Cannot read RTC time");
        return true;
    }
    
    // Check for year < 2024 (factory default or uninitialized)
    int rtcYear = rtcTime.tm_year + 1900;
    if (rtcYear < 2024) {
        Serial.printf("[RX8900] RTC year %d < 2024 - needs sync\n", rtcYear);
        return true;
    }
    
    // Compare with compile time
    time_t rtcEpoch = mktime(&rtcTime);
    time_t compileEpoch = getCompileEpoch();
    
    if (compileEpoch > 0 && rtcEpoch < compileEpoch) {
        Serial.println("[RX8900] RTC time is older than compile time");
        return true;
    }
    
    return false;
}

void formatTime(time_t epoch, char* buf, size_t bufLen) {
    struct tm* t = localtime(&epoch);
    if (t && buf && bufLen >= 20) {
        snprintf(buf, bufLen, "%04d-%02d-%02d %02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
    } else if (buf && bufLen > 0) {
        buf[0] = '\0';
    }
}

} // namespace RX8900CE

