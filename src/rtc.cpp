#include "rtc.h"

#include <Wire.h>
#include <cstdio>
#include <cstring>

// ---- Low-level helpers ----

static RX8900 rtcDevice;
static volatile bool rtcUpdatePending = false;

// RX8900 I2C 7-bit address (our own name to avoid conflicts)
static const uint8_t RTC_I2C_ADDRESS = 0x32;

// Register addresses (basic time/calendar bank)
static const uint8_t RX8900_REG_SEC = 0x00;
static const uint8_t RX8900_REG_MIN = 0x01;
static const uint8_t RX8900_REG_HOUR = 0x02;
static const uint8_t RX8900_REG_WEEK = 0x03;
static const uint8_t RX8900_REG_DAY = 0x04;
static const uint8_t RX8900_REG_MONTH = 0x05;
static const uint8_t RX8900_REG_YEAR = 0x06;

// BCD helpers
static uint8_t decToBcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static uint8_t bcdToDec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

// 0=Sunday .. 6=Saturday (Sakamoto's algorithm)
static uint8_t computeWeekdayIndex(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3)
    {
        year -= 1;
    }
    uint8_t dow = (uint8_t)((year + year / 4 - year / 100 + year / 400 +
                             t[month - 1] + day) %
                            7);
    return dow; // 0=Sunday
}

// Convert weekday index (0=Sun..6=Sat) to RX8900 WEEK register bit-field.
// Sunday=0x01, Monday=0x02, Tuesday=0x04, Wednesday=0x08,
// Thursday=0x10, Friday=0x20, Saturday=0x40.
static uint8_t weekdayIndexToWeekReg(uint8_t weekdayIndex)
{
    if (weekdayIndex > 6)
        weekdayIndex = 0;
    return (uint8_t)(1U << weekdayIndex);
}

// Convert RX8900 WEEK register bit-field to weekday index 0..6.
// If multiple bits are set, uses the lowest set bit.
static uint8_t weekRegToWeekdayIndex(uint8_t weekReg)
{
    if (weekReg == 0)
        return 0;

    for (uint8_t i = 0; i < 7; ++i)
    {
        if (weekReg & (1U << i))
            return i;
    }
    return 0;
}

// I2C write sequence: addr, startReg, data[len]
static bool rtcWriteRegisters(uint8_t startReg, const uint8_t *data, size_t length)
{
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Wire.write(startReg);
    Wire.write(data, length);
    return (Wire.endTransmission() == 0);
}

// I2C read sequence: write startReg, then read len bytes
static bool rtcReadRegisters(uint8_t startReg, uint8_t *data, size_t length)
{
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Wire.write(startReg);
    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }

    size_t readCount = Wire.requestFrom(RTC_I2C_ADDRESS, (uint8_t)length);
    if (readCount != length)
    {
        return false;
    }

    for (size_t i = 0; i < length; ++i)
    {
        data[i] = Wire.read();
    }
    return true;
}

// Parse __DATE__ / __TIME__ into RtcDateTime.
// __DATE__ is "Mmm dd yyyy", __TIME__ is "hh:mm:ss".
static bool parseCompileDateTime(RtcDateTime &dt)
{
    const char *dateStr = __DATE__;
    const char *timeStr = __TIME__;

    char monthStr[4] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (std::sscanf(dateStr, "%3s %d %d", monthStr, &day, &year) != 3)
    {
        Serial.println("[RTC] Failed to parse __DATE__.");
        return false;
    }

    if (std::sscanf(timeStr, "%d:%d:%d", &hour, &minute, &second) != 3)
    {
        Serial.println("[RTC] Failed to parse __TIME__.");
        return false;
    }

    uint8_t month = 0;
    if (std::strcmp(monthStr, "Jan") == 0)
        month = 1;
    else if (std::strcmp(monthStr, "Feb") == 0)
        month = 2;
    else if (std::strcmp(monthStr, "Mar") == 0)
        month = 3;
    else if (std::strcmp(monthStr, "Apr") == 0)
        month = 4;
    else if (std::strcmp(monthStr, "May") == 0)
        month = 5;
    else if (std::strcmp(monthStr, "Jun") == 0)
        month = 6;
    else if (std::strcmp(monthStr, "Jul") == 0)
        month = 7;
    else if (std::strcmp(monthStr, "Aug") == 0)
        month = 8;
    else if (std::strcmp(monthStr, "Sep") == 0)
        month = 9;
    else if (std::strcmp(monthStr, "Oct") == 0)
        month = 10;
    else if (std::strcmp(monthStr, "Nov") == 0)
        month = 11;
    else if (std::strcmp(monthStr, "Dec") == 0)
        month = 12;

    if (month == 0)
    {
        Serial.println("[RTC] Failed to map month string.");
        return false;
    }

    dt.year = (uint16_t)year;
    dt.month = (uint8_t)month;
    dt.day = (uint8_t)day;
    dt.hour = (uint8_t)hour;
    dt.minute = (uint8_t)minute;
    dt.second = (uint8_t)second;
    dt.weekday = computeWeekdayIndex(dt.year, dt.month, dt.day);

    return true;
}

// ---- Interrupt handling ----

void IRAM_ATTR rtcIntIsr()
{
    rtcUpdatePending = true;
}

// ---- Public API ----

bool rtcInit()
{
    Serial.println("[INIT][RTC] Initialising RX8900...");

    pinMode(PIN_RTC_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_RTC_INT), rtcIntIsr, FALLING);

    // Library-level init for control/flag registers, etc.
    rtcDevice.RX8900Init();

    // false = 1-second update, true = 1-minute update
    rtcDevice.UpdateInterruptTimingChange(false);

    // InterruptSettings(AIE, TIE, UIE) – only enable Update Interrupt
    rtcDevice.InterruptSettings(false, false, true);

    // Set RTC from compile time each boot (dev-friendly).
    if (!rtcSetFromCompileTime())
    {
        Serial.println("[INIT][RTC] WARNING: Failed to set RTC from compile time.");
    }
    else
    {
        Serial.print("[INIT][RTC] Set from compile time: ");
        Serial.print(__DATE__);
        Serial.print(" ");
        Serial.println(__TIME__);
    }

    Serial.println("[INIT][RTC] RX8900 configured for 1 Hz update interrupt.");
    return true;
}

void rtcHandleUpdate()
{
    if (!rtcUpdatePending)
        return;

    rtcUpdatePending = false;

    uint8_t flagChange = 0;
    rtcDevice.JudgeInterruptSignalType(&flagChange);

    Serial.print("[RTC] flagChange: 0b");
    Serial.println(flagChange, BIN);

    // Clear only the Update Flag (UF). AF/TF remain untouched.
    rtcDevice.ClearOccurrenceNotification(false, false, true);
}

RX8900 &rtcGetDevice()
{
    return rtcDevice;
}

bool rtcSetDateTime(const RtcDateTime &dt)
{
    if (dt.year < 2000 || dt.year > 2099)
    {
        Serial.println("[RTC] Year out of range (2000-2099).");
        return false;
    }

    uint8_t year2 = (uint8_t)(dt.year % 100);

    uint8_t buf[7];
    buf[0] = decToBcd(dt.second);
    buf[1] = decToBcd(dt.minute);
    buf[2] = decToBcd(dt.hour);
    buf[3] = weekdayIndexToWeekReg(computeWeekdayIndex(dt.year, dt.month, dt.day));
    buf[4] = decToBcd(dt.day);
    buf[5] = decToBcd(dt.month);
    buf[6] = decToBcd(year2);

    if (!rtcWriteRegisters(RX8900_REG_SEC, buf, sizeof(buf)))
    {
        Serial.println("[RTC] Failed to write time registers.");
        return false;
    }

    Serial.println("[RTC] Date/time written to RX8900.");
    return true;
}

bool rtcGetDateTime(RtcDateTime &dt)
{
    uint8_t buf[7] = {0};
    if (!rtcReadRegisters(RX8900_REG_SEC, buf, sizeof(buf)))
    {
        Serial.println("[RTC] Failed to read time registers.");
        return false;
    }

    dt.second = bcdToDec((uint8_t)(buf[0] & 0x7F));
    dt.minute = bcdToDec((uint8_t)(buf[1] & 0x7F));
    dt.hour = bcdToDec((uint8_t)(buf[2] & 0x3F));
    dt.day = bcdToDec((uint8_t)(buf[4] & 0x3F));
    dt.month = bcdToDec((uint8_t)(buf[5] & 0x1F));
    dt.year = (uint16_t)(2000 + bcdToDec(buf[6]));

    uint8_t weekReg = buf[3];
    dt.weekday = weekRegToWeekdayIndex(weekReg);

    return true;
}

bool rtcSetFromCompileTime()
{
    RtcDateTime dt;
    if (!parseCompileDateTime(dt))
    {
        return false;
    }
    return rtcSetDateTime(dt);
}

// ---- Sample timebase helpers ----

void rtcInitSampleTimebase(SampleTimebase &timebase,
                           const RtcDateTime &rtcNow,
                           uint64_t sampleIndexNow,
                           uint32_t sampleRate)
{
    timebase.anchorRtc = rtcNow;
    timebase.anchorSampleIndex = sampleIndexNow;
    timebase.sampleRate = sampleRate;
}

double rtcSampleIndexToSeconds(const SampleTimebase &timebase,
                               uint64_t sampleIndex)
{
    int64_t deltaSamples = (int64_t)sampleIndex - (int64_t)timebase.anchorSampleIndex;
    return (double)deltaSamples / (double)timebase.sampleRate;
}
