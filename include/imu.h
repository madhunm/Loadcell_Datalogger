#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_LSM6DSV16X.h>
#include "pins.h"

// Global IMU instance (defined in imu.cpp)
extern SparkFun_LSM6DSV16X imuDevice;

// Initialise the IMU.
// - Assumes Wire.begin(...) was already called in main.cpp with the correct SDA/SCL pins.
// - Returns true on success, false if the device does not respond or config fails.
bool imuInit(TwoWire &wire = Wire);

// Read one accel/gyro sample if new data is available.
// Returns true and fills the output refs if a fresh sample was read,
// or false if no new data was ready.
bool imuRead(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz);
