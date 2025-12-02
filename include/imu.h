#pragma once

#include <Arduino.h>
#include <Wire.h>

// Forward declare the IMU object type so users of this header don't need
// to include the SparkFun header directly unless they want to.
#include <SparkFun_LSM6DSV16X.h>

// Global IMU instance (defined in imu.cpp)
extern SparkFun_LSM6DSV16X g_imu;

// Pin definitions for the IMU interrupts.
// Adjust these if your pin naming is different.
constexpr int IMU_INT1_PIN = 39;   // Connected to LSM6DSV INT1
constexpr int IMU_INT2_PIN = 40;   // Connected to LSM6DSV INT2

// Call once during startup, after Wire.begin().
bool imu_init(TwoWire &wire = Wire);

// Optional helper to read one sample of accel/gyro (for testing)
bool imu_read(float &ax, float &ay, float &az,
              float &gx, float &gy, float &gz);
