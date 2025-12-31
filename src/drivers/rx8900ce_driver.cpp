/**
 * @file rx8900ce_driver.cpp
 * @brief Implementation of RX8900CE RTC driver
 */

#include "rx8900ce_driver.h"

// Days in each month
static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool isLeapYear(uint16_t year) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

uint32_t DateTime::toUnixTime() const {
    uint32_t t;
    uint16_t days = 0;
    
    // Days from 1970 to the beginning of this year
    for (uint16_t y = 1970; y < year; y++) {
        days += isLeapYear(y) ? 366 : 365;
    }
    
    // Days from beginning of year to beginning of month
    for (uint8_t m = 1; m < month; m++) {
        days += daysInMonth[m - 1];
        if (m == 2 && isLeapYear(year)) {
            days++;  // Add leap day
        }
    }
    
    // Days in this month
    days += day - 1;
    
    // Convert to seconds
    t = days * 86400UL;
    t += hour * 3600UL;
    t += minute * 60UL;
    t += second;
    
    return t;
}

void DateTime::fromUnixTime(uint32_t t) {
    second = t % 60;
    t /= 60;
    minute = t % 60;
    t /= 60;
    hour = t % 24;
    uint16_t days = t / 24;
    
    year = 1970;
    while (days >= (uint16_t)(isLeapYear(year) ? 366 : 365)) {
        days -= isLeapYear(year) ? 366 : 365;
        year++;
    }
    
    month = 1;
    while (days >= daysInMonth[month - 1]) {
        if (month == 2 && isLeapYear(year)) {
            if (days >= 29) {
                days -= 29;
                month++;
            } else {
                break;
            }
        } else {
            days -= daysInMonth[month - 1];
            month++;
        }
    }
    
    day = days + 1;
}

uint8_t RX8900CEDriver::bcdToDec(uint8_t bcd) {
    return (bcd / 16 * 10) + (bcd % 16);
}

uint8_t RX8900CEDriver::decToBcd(uint8_t dec) {
    return (dec / 10 * 16) + (dec % 10);
}

bool RX8900CEDriver::writeRegister(uint8_t reg, uint8_t value) {
    wire->beginTransmission(i2c_addr);
    wire->write(reg);
    wire->write(value);
    return (wire->endTransmission() == 0);
}

bool RX8900CEDriver::readRegister(uint8_t reg, uint8_t& value) {
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

bool RX8900CEDriver::readRegisters(uint8_t reg, uint8_t* buffer, uint8_t len) {
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

bool RX8900CEDriver::begin(TwoWire* wire_obj, uint8_t addr) {
    wire = wire_obj;
    i2c_addr = addr;
    initialized = false;
    
    // Test communication by reading a register
    uint8_t test;
    if (!readRegister(RX8900_REG_SEC, test)) {
        return false;
    }
    
    // Enable 1Hz output by default
    if (!enable1HzOutput()) {
        return false;
    }
    
    initialized = true;
    return true;
}

bool RX8900CEDriver::setDateTime(const DateTime& dt) {
    uint8_t buffer[7];
    
    buffer[0] = decToBcd(dt.second);
    buffer[1] = decToBcd(dt.minute);
    buffer[2] = decToBcd(dt.hour);
    buffer[3] = 0;  // Week day (not used)
    buffer[4] = decToBcd(dt.day);
    buffer[5] = decToBcd(dt.month);
    buffer[6] = decToBcd(dt.year - 2000);  // RTC stores year as offset from 2000
    
    // Write all time registers at once
    wire->beginTransmission(i2c_addr);
    wire->write(RX8900_REG_SEC);
    for (uint8_t i = 0; i < 7; i++) {
        wire->write(buffer[i]);
    }
    
    return (wire->endTransmission() == 0);
}

bool RX8900CEDriver::getDateTime(DateTime& dt) {
    uint8_t buffer[7];
    
    if (!readRegisters(RX8900_REG_SEC, buffer, 7)) {
        return false;
    }
    
    dt.second = bcdToDec(buffer[0] & 0x7F);
    dt.minute = bcdToDec(buffer[1] & 0x7F);
    dt.hour = bcdToDec(buffer[2] & 0x3F);
    dt.day = bcdToDec(buffer[4] & 0x3F);
    dt.month = bcdToDec(buffer[5] & 0x1F);
    dt.year = bcdToDec(buffer[6]) + 2000;
    
    return true;
}

bool RX8900CEDriver::enable1HzOutput() {
    // Extension register: set bit 5 (FSEL1) and clear bit 4 (FSEL0) for 1Hz
    uint8_t ext_reg;
    if (!readRegister(RX8900_REG_EXT, ext_reg)) {
        return false;
    }
    
    ext_reg |= (1 << 5);   // Set FSEL1
    ext_reg &= ~(1 << 4);  // Clear FSEL0
    
    if (!writeRegister(RX8900_REG_EXT, ext_reg)) {
        return false;
    }
    
    // Control register: enable FOUT (bit 6)
    uint8_t ctrl_reg;
    if (!readRegister(RX8900_REG_CTRL, ctrl_reg)) {
        return false;
    }
    
    ctrl_reg |= (1 << 6);  // Enable FOUT
    
    return writeRegister(RX8900_REG_CTRL, ctrl_reg);
}

bool RX8900CEDriver::disableFoutOutput() {
    uint8_t ctrl_reg;
    if (!readRegister(RX8900_REG_CTRL, ctrl_reg)) {
        return false;
    }
    
    ctrl_reg &= ~(1 << 6);  // Disable FOUT
    
    return writeRegister(RX8900_REG_CTRL, ctrl_reg);
}

bool RX8900CEDriver::enableUpdateInterrupt() {
    uint8_t ctrl_reg;
    if (!readRegister(RX8900_REG_CTRL, ctrl_reg)) {
        return false;
    }
    
    ctrl_reg |= (1 << 5);  // Enable update interrupt (UIE)
    
    return writeRegister(RX8900_REG_CTRL, ctrl_reg);
}

bool RX8900CEDriver::disableUpdateInterrupt() {
    uint8_t ctrl_reg;
    if (!readRegister(RX8900_REG_CTRL, ctrl_reg)) {
        return false;
    }
    
    ctrl_reg &= ~(1 << 5);  // Disable update interrupt
    
    return writeRegister(RX8900_REG_CTRL, ctrl_reg);
}

bool RX8900CEDriver::isRunning() {
    uint8_t flag_reg;
    if (!readRegister(RX8900_REG_FLAG, flag_reg)) {
        return false;
    }
    
    // Bit 5: VLF (Voltage Low Flag) - 0 = normal, 1 = voltage low
    return (flag_reg & (1 << 5)) == 0;
}

uint32_t RX8900CEDriver::getUnixTime() {
    DateTime dt;
    if (!getDateTime(dt)) {
        return 0;
    }
    return dt.toUnixTime();
}

bool RX8900CEDriver::setUnixTime(uint32_t timestamp) {
    DateTime dt;
    dt.fromUnixTime(timestamp);
    return setDateTime(dt);
}
