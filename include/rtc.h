#pragma once

#include <Arduino.h>
#include "RX8900.h"
#include "pins.h"

// Initialise the RX8900 RTC and 1 Hz update interrupt.
// Returns true on success.
bool rtcInit();

// Call this regularly (e.g. each loop()) to service the 1 Hz RTC update
// interrupt and clear flags inside the RX8900.
void rtcHandleUpdate();

// Access to the underlying RX8900 device, if needed elsewhere.
RX8900 &rtcGetDevice();
