#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_LSM6DSV16X.h>

// Global IMU instance (defined in imu.cpp)
extern SparkFun_LSM6DSV16X g_imu;

// Pin definitions for the IMU interrupts.
constexpr int IMU_INT1_PIN = 39;   // Connected to LSM6DSV INT1
constexpr int IMU_INT2_PIN = 40;   // Connected to LSM6DSV INT2

// Call once during startup, after Wire.begin().
bool imuInit(TwoWire &wire = Wire);

// Optional helper to read one sample of accel/gyro (for testing).
bool imuRead(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz);

// ---- IMU sampling ring buffer ----

struct ImuSample
{
    uint32_t index;           // IMU sample index
    uint32_t adcSampleIndex;  // ADC sample index at time of read
    float ax, ay, az;         // accelerometer
    float gx, gy, gz;         // gyro
};

// Start IMU sampling task pinned to a given core (core 0 for you).
// The ADC sampling task should have higher priority; IMU uses medium.
void imuStartSamplingTask(UBaseType_t coreId = 0);

// Pop next IMU sample from ring buffer. Returns true if available.
bool imuGetNextSample(ImuSample &sample);

// Approx number of IMU samples currently buffered.
size_t imuGetBufferedSampleCount();

// Number of times the IMU ring buffer overflowed (samples dropped).
size_t imuGetOverflowCount();
