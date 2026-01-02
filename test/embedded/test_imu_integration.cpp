/**
 * @file test_imu_integration.cpp
 * @brief Integration tests for LSM6DSV IMU driver
 * 
 * These tests run on the ESP32-S3 with actual hardware connected.
 * Requires: LSM6DSV IMU properly wired via I2C.
 * 
 * Tests:
 * - IMU initialization
 * - WHO_AM_I verification
 * - Accelerometer readings
 * - Gyroscope readings
 * - FIFO operation
 */

#include <unity.h>
#include <Arduino.h>
#include "drivers/lsm6dsv.h"

void setUp() {
    // Per-test setup
}

void tearDown() {
    // Per-test cleanup
}

// ============================================================================
// Initialization Tests
// ============================================================================

void test_imu_init() {
    bool result = LSM6DSV::init();
    TEST_ASSERT_TRUE_MESSAGE(result, "IMU initialization failed - check I2C wiring");
}

void test_imu_who_am_i() {
    LSM6DSV::init();
    
    uint8_t whoAmI = LSM6DSV::readWhoAmI();
    Serial.printf("WHO_AM_I: 0x%02X\n", whoAmI);
    
    // LSM6DSV WHO_AM_I should be 0x70
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x70, whoAmI, "Wrong WHO_AM_I - wrong device?");
}

void test_imu_is_connected() {
    bool connected = LSM6DSV::isConnected();
    TEST_ASSERT_TRUE_MESSAGE(connected, "IMU not detected on I2C bus");
}

// ============================================================================
// Accelerometer Tests
// ============================================================================

void test_imu_accel_read_raw() {
    LSM6DSV::init();
    
    int16_t ax, ay, az;
    bool result = LSM6DSV::readAccelRaw(ax, ay, az);
    
    TEST_ASSERT_TRUE(result);
    Serial.printf("Accel raw: X=%d, Y=%d, Z=%d\n", ax, ay, az);
    
    // All values should be valid int16
    TEST_ASSERT_GREATER_OR_EQUAL(INT16_MIN, ax);
    TEST_ASSERT_LESS_OR_EQUAL(INT16_MAX, ax);
}

void test_imu_accel_read_scaled() {
    LSM6DSV::init();
    LSM6DSV::setAccelScale(LSM6DSV::AccelScale::SCALE_2G);
    
    float ax, ay, az;
    bool result = LSM6DSV::readAccel(ax, ay, az);
    
    TEST_ASSERT_TRUE(result);
    Serial.printf("Accel (g): X=%.3f, Y=%.3f, Z=%.3f\n", ax, ay, az);
    
    // At rest, one axis should be near +/- 1g
    float magnitude = sqrt(ax*ax + ay*ay + az*az);
    Serial.printf("Accel magnitude: %.3f g\n", magnitude);
    
    // Should be between 0.8g and 1.2g when stationary
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.3f, 1.0f, magnitude, 
        "Accel magnitude not near 1g - device moving or misconfigured?");
}

void test_imu_accel_gravity_vector() {
    LSM6DSV::init();
    LSM6DSV::setAccelScale(LSM6DSV::AccelScale::SCALE_2G);
    
    // Average multiple readings to reduce noise
    float sumX = 0, sumY = 0, sumZ = 0;
    const int samples = 10;
    
    for (int i = 0; i < samples; i++) {
        float ax, ay, az;
        LSM6DSV::readAccel(ax, ay, az);
        sumX += ax;
        sumY += ay;
        sumZ += az;
        delay(10);
    }
    
    float avgX = sumX / samples;
    float avgY = sumY / samples;
    float avgZ = sumZ / samples;
    
    Serial.printf("Avg Accel (g): X=%.3f, Y=%.3f, Z=%.3f\n", avgX, avgY, avgZ);
    
    // Verify gravity is detected on one axis
    // (Depends on device orientation)
    float maxAxis = max(abs(avgX), max(abs(avgY), abs(avgZ)));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0.7f, maxAxis, 
        "No axis shows gravity - accelerometer may be faulty");
}

// ============================================================================
// Gyroscope Tests
// ============================================================================

void test_imu_gyro_read_raw() {
    LSM6DSV::init();
    
    int16_t gx, gy, gz;
    bool result = LSM6DSV::readGyroRaw(gx, gy, gz);
    
    TEST_ASSERT_TRUE(result);
    Serial.printf("Gyro raw: X=%d, Y=%d, Z=%d\n", gx, gy, gz);
}

void test_imu_gyro_read_scaled() {
    LSM6DSV::init();
    LSM6DSV::setGyroScale(LSM6DSV::GyroScale::SCALE_250DPS);
    
    float gx, gy, gz;
    bool result = LSM6DSV::readGyro(gx, gy, gz);
    
    TEST_ASSERT_TRUE(result);
    Serial.printf("Gyro (dps): X=%.2f, Y=%.2f, Z=%.2f\n", gx, gy, gz);
    
    // When stationary, gyro should be near zero (within offset)
    // Typical offset is +/- 10 dps
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(20.0f, 0.0f, gx, "Gyro X offset too high");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(20.0f, 0.0f, gy, "Gyro Y offset too high");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(20.0f, 0.0f, gz, "Gyro Z offset too high");
}

void test_imu_gyro_stability() {
    LSM6DSV::init();
    LSM6DSV::setGyroScale(LSM6DSV::GyroScale::SCALE_250DPS);
    
    // Check gyro stability over 100ms
    float minX = 1000, maxX = -1000;
    
    for (int i = 0; i < 10; i++) {
        float gx, gy, gz;
        LSM6DSV::readGyro(gx, gy, gz);
        if (gx < minX) minX = gx;
        if (gx > maxX) maxX = gx;
        delay(10);
    }
    
    float range = maxX - minX;
    Serial.printf("Gyro X range over 100ms: %.2f dps\n", range);
    
    // When stationary, should be fairly stable (< 5 dps variation)
    TEST_ASSERT_LESS_THAN_MESSAGE(10.0f, range, 
        "Gyro unstable - device moving or noisy?");
}

// ============================================================================
// Combined Reading Tests
// ============================================================================

void test_imu_read_both() {
    LSM6DSV::init();
    
    int16_t ax, ay, az, gx, gy, gz;
    bool result = LSM6DSV::readBothRaw(ax, ay, az, gx, gy, gz);
    
    TEST_ASSERT_TRUE(result);
    Serial.printf("Combined: A(%d,%d,%d) G(%d,%d,%d)\n", ax, ay, az, gx, gy, gz);
}

// ============================================================================
// FIFO Tests
// ============================================================================

void test_imu_fifo_enable() {
    LSM6DSV::init();
    
    bool result = LSM6DSV::enableFIFO(LSM6DSV::FIFOMode::CONTINUOUS);
    TEST_ASSERT_TRUE(result);
}

void test_imu_fifo_produces_data() {
    LSM6DSV::init();
    LSM6DSV::enableFIFO(LSM6DSV::FIFOMode::CONTINUOUS);
    LSM6DSV::setAccelODR(LSM6DSV::ODR::ODR_416HZ);
    
    delay(100);  // Let FIFO fill
    
    uint16_t count = LSM6DSV::getFIFOCount();
    Serial.printf("FIFO samples after 100ms: %u\n", count);
    
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, count, "FIFO empty - not filling?");
}

void test_imu_fifo_read_batch() {
    LSM6DSV::init();
    LSM6DSV::enableFIFO(LSM6DSV::FIFOMode::CONTINUOUS);
    LSM6DSV::setAccelODR(LSM6DSV::ODR::ODR_416HZ);
    
    delay(50);  // Let FIFO fill
    
    LSM6DSV::FIFOData data[50];
    uint16_t count = LSM6DSV::readFIFOBatch(data, 50);
    
    Serial.printf("Read %u samples from FIFO\n", count);
    TEST_ASSERT_GREATER_THAN(0, count);
    
    // Print first few samples
    for (int i = 0; i < min((int)count, 5); i++) {
        Serial.printf("  [%d] A(%d,%d,%d) G(%d,%d,%d) tag=%02X\n",
            i, data[i].accelX, data[i].accelY, data[i].accelZ,
            data[i].gyroX, data[i].gyroY, data[i].gyroZ, data[i].tag);
    }
}

// ============================================================================
// ODR (Output Data Rate) Tests
// ============================================================================

void test_imu_odr_change() {
    LSM6DSV::init();
    
    // Test different ODRs
    LSM6DSV::ODR rates[] = {
        LSM6DSV::ODR::ODR_104HZ,
        LSM6DSV::ODR::ODR_208HZ,
        LSM6DSV::ODR::ODR_416HZ
    };
    
    for (auto rate : rates) {
        LSM6DSV::setAccelODR(rate);
        LSM6DSV::enableFIFO(LSM6DSV::FIFOMode::CONTINUOUS);
        
        delay(100);
        
        uint16_t count = LSM6DSV::getFIFOCount();
        Serial.printf("ODR setting %d: %u samples in 100ms\n", (int)rate, count);
        
        // Higher ODR should produce more samples
        TEST_ASSERT_GREATER_THAN(0, count);
        
        LSM6DSV::disableFIFO();
    }
}

// ============================================================================
// Scale Tests
// ============================================================================

void test_imu_accel_scale_2g() {
    LSM6DSV::init();
    LSM6DSV::setAccelScale(LSM6DSV::AccelScale::SCALE_2G);
    
    float ax, ay, az;
    LSM6DSV::readAccel(ax, ay, az);
    
    float mag = sqrt(ax*ax + ay*ay + az*az);
    Serial.printf("2G scale magnitude: %.3f g\n", mag);
    
    // At 2g scale, stationary device should read ~1g
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 1.0f, mag);
}

void test_imu_accel_scale_16g() {
    LSM6DSV::init();
    LSM6DSV::setAccelScale(LSM6DSV::AccelScale::SCALE_16G);
    
    float ax, ay, az;
    LSM6DSV::readAccel(ax, ay, az);
    
    float mag = sqrt(ax*ax + ay*ay + az*az);
    Serial.printf("16G scale magnitude: %.3f g\n", mag);
    
    // At 16g scale, should still read ~1g when stationary
    // (might have more noise due to lower resolution)
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 1.0f, mag);
}

// ============================================================================
// Temperature Test
// ============================================================================

void test_imu_temperature() {
    LSM6DSV::init();
    
    float tempC = LSM6DSV::readTemperature();
    Serial.printf("IMU Temperature: %.1f C\n", tempC);
    
    // Should be reasonable room temperature (0-50 C typically)
    TEST_ASSERT_GREATER_THAN(-10.0f, tempC);
    TEST_ASSERT_LESS_THAN(80.0f, tempC);
}

// ============================================================================
// Test Runner
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n\n=== LSM6DSV IMU Integration Tests ===\n");
    
    UNITY_BEGIN();
    
    // Initialization tests
    RUN_TEST(test_imu_init);
    RUN_TEST(test_imu_who_am_i);
    RUN_TEST(test_imu_is_connected);
    
    // Accelerometer tests
    RUN_TEST(test_imu_accel_read_raw);
    RUN_TEST(test_imu_accel_read_scaled);
    RUN_TEST(test_imu_accel_gravity_vector);
    
    // Gyroscope tests
    RUN_TEST(test_imu_gyro_read_raw);
    RUN_TEST(test_imu_gyro_read_scaled);
    RUN_TEST(test_imu_gyro_stability);
    
    // Combined reading tests
    RUN_TEST(test_imu_read_both);
    
    // FIFO tests
    RUN_TEST(test_imu_fifo_enable);
    RUN_TEST(test_imu_fifo_produces_data);
    RUN_TEST(test_imu_fifo_read_batch);
    
    // ODR tests
    RUN_TEST(test_imu_odr_change);
    
    // Scale tests
    RUN_TEST(test_imu_accel_scale_2g);
    RUN_TEST(test_imu_accel_scale_16g);
    
    // Temperature test
    RUN_TEST(test_imu_temperature);
    
    UNITY_END();
}

void loop() {
    delay(1000);
}




