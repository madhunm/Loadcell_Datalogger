#pragma once

#include <Arduino.h>
#include "RX8900.h"
#include "pins.h"

// Simple date/time container for the RTC.
// year:   2000-2099
// month:  1-12
// day:    1-31
// hour:   0-23
// minute: 0-59
// second: 0-59
// weekday: 0=Sunday .. 6=Saturday (derived internally)
struct RtcDateTime
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
};

// Initialise the RX8900 RTC, configure 1 Hz update interrupt,
// and set the time from the firmware compile time.
// Returns true on success.
bool rtcInit();

// Call this regularly (e.g. each loop()) to service the 1 Hz RTC update
// interrupt and clear flags inside the RX8900.
void rtcHandleUpdate();

// Access to the underlying RX8900 device, if needed elsewhere.
RX8900 &rtcGetDevice();

// Set the RTC date/time.
// Returns true on success.
bool rtcSetDateTime(const RtcDateTime &dt);

// Read the current date/time from the RTC.
// Returns true on success.
bool rtcGetDateTime(RtcDateTime &dt);

// Set RTC from the firmware compile time (__DATE__ / __TIME__).
// Returns true on success.
bool rtcSetFromCompileTime();
