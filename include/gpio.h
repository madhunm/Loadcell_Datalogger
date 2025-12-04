#pragma once

#include <Arduino.h>

// Initialize all GPIO pins to their correct directions and states.
// This should be called once during setup() before initializing peripherals.
void gpioInit();

