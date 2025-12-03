#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_LSM6DSV16X.h>
#include "pins.h"

// Global IMU instance (defined in imu.cpp)
extern SparkFun_LSM6DSV16X imuDevice;

// Initialise the IMU.
// Assumes Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL) was already called.
bool imuInit(TwoWire &wire = Wire);

// Read one accel/gyro sample if new data is available.
// Returns true and fills the outputs, or false if no fresh sample.
bool imuRead(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz);
