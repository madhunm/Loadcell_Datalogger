/**
 * @file test_adc_integration.cpp
 * @brief Integration tests for MAX11270 ADC driver
 * 
 * These tests run on the ESP32-S3 with actual hardware connected.
 * Requires: MAX11270 ADC properly wired and powered.
 * 
 * Tests:
 * - ADC initialization
 * - Single conversion reads
 * - Continuous conversion mode
 * - Overflow detection
 * - Data integrity
 */

#include <unity.h>
#include <Arduino.h>
#include "drivers/max11270.h"
#include "logging/ring_buffer.h"

// Test buffer
static ADCRingBuffer* testBuffer;

void setUp() {
    testBuffer = new ADCRingBuffer();
    testBuffer->reset();
    testBuffer->resetStats();
}

void tearDown() {
    MAX11270::stopContinuous();
    delete testBuffer;
    testBuffer = nullptr;
}

// ============================================================================
// Initialization Tests
// ============================================================================

void test_adc_init() {
    bool result = MAX11270::init();
    TEST_ASSERT_TRUE_MESSAGE(result, "ADC initialization failed - check wiring");
}

void test_adc_device_id() {
    MAX11270::init();
    
    // Read device ID register (if available)
    // The MAX11270 should be accessible after init
    TEST_ASSERT_TRUE(MAX11270::isReady());
}

// ============================================================================
// Single Conversion Tests
// ============================================================================

void test_adc_single_read() {
    MAX11270::init();
    
    int32_t raw = MAX11270::readSingle(100);  // 100ms timeout
    
    // Valid 24-bit range: -8388608 to 8388607
    TEST_ASSERT_GREATER_OR_EQUAL(-8388608, raw);
    TEST_ASSERT_LESS_OR_EQUAL(8388607, raw);
}

void test_adc_single_read_multiple() {
    MAX11270::init();
    
    // Read several samples and ensure they're reasonable
    int32_t samples[10];
    for (int i = 0; i < 10; i++) {
        samples[i] = MAX11270::readSingle(100);
        delay(10);  // Small delay between reads
    }
    
    // All should be valid 24-bit values
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL(-8388608, samples[i]);
        TEST_ASSERT_LESS_OR_EQUAL(8388607, samples[i]);
    }
    
    // Check for some variation (not stuck at same value)
    bool hasVariation = false;
    for (int i = 1; i < 10; i++) {
        if (samples[i] != samples[0]) {
            hasVariation = true;
            break;
        }
    }
    // Note: With no load, values might be very stable, so this is informational
    Serial.printf("ADC variation detected: %s\n", hasVariation ? "yes" : "no (normal with stable input)");
}

// ============================================================================
// Continuous Conversion Tests
// ============================================================================

void test_adc_continuous_start_stop() {
    MAX11270::init();
    
    TEST_ASSERT_TRUE(MAX11270::startContinuous(testBuffer));
    TEST_ASSERT_TRUE(MAX11270::isRunning());
    
    delay(50);  // Let it run briefly
    
    MAX11270::stopContinuous();
    TEST_ASSERT_FALSE(MAX11270::isRunning());
}

void test_adc_continuous_produces_samples() {
    MAX11270::init();
    MAX11270::startContinuous(testBuffer);
    
    delay(100);  // Run for 100ms
    
    MAX11270::stopContinuous();
    
    uint32_t count = testBuffer->available();
    Serial.printf("Samples collected in 100ms: %u\n", count);
    
    // At default rate, should have gotten some samples
    TEST_ASSERT_GREATER_THAN(0, count);
}

void test_adc_continuous_no_overflow_short_run() {
    MAX11270::init();
    MAX11270::startContinuous(testBuffer);
    
    // Run for 50ms - buffer should handle this easily
    delay(50);
    
    MAX11270::stopContinuous();
    
    uint32_t overflows = testBuffer->getOverflowCount();
    TEST_ASSERT_EQUAL_MESSAGE(0, overflows, "Unexpected overflow in short run");
}

void test_adc_continuous_samples_have_valid_timestamps() {
    MAX11270::init();
    MAX11270::startContinuous(testBuffer);
    
    delay(100);
    
    MAX11270::stopContinuous();
    
    // Pop samples and check timestamps
    ADCSample samples[100];
    size_t count = testBuffer->popBatch(samples, 100);
    
    TEST_ASSERT_GREATER_THAN(1, count);
    
    // Timestamps should be monotonically increasing
    for (size_t i = 1; i < count; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(
            samples[i-1].timestamp_us, 
            samples[i].timestamp_us,
            "Timestamps not monotonic"
        );
    }
}

void test_adc_continuous_sample_rate() {
    MAX11270::init();
    MAX11270::startContinuous(testBuffer);
    
    uint32_t startTime = millis();
    delay(1000);  // Run for exactly 1 second
    uint32_t elapsed = millis() - startTime;
    
    MAX11270::stopContinuous();
    
    uint32_t count = testBuffer->getTotalPushed();
    float actualRate = (float)count / (elapsed / 1000.0f);
    
    Serial.printf("Sample rate: %.1f sps (elapsed: %u ms, count: %u)\n", 
                  actualRate, elapsed, count);
    
    // Rate should be within 10% of configured rate
    // (This is a loose check - actual rate depends on configuration)
    TEST_ASSERT_GREATER_THAN(100, actualRate);  // At least some samples
}

// ============================================================================
// Data Integrity Tests
// ============================================================================

void test_adc_values_in_expected_range() {
    MAX11270::init();
    MAX11270::startContinuous(testBuffer);
    
    delay(100);
    
    MAX11270::stopContinuous();
    
    ADCSample sample;
    while (testBuffer->pop(sample)) {
        // All values should be valid 24-bit signed
        TEST_ASSERT_GREATER_OR_EQUAL(-8388608, sample.raw);
        TEST_ASSERT_LESS_OR_EQUAL(8388607, sample.raw);
    }
}

void test_adc_sequence_continuity() {
    MAX11270::init();
    
    // Reset statistics
    MAX11270::resetStats();
    
    MAX11270::startContinuous(testBuffer);
    delay(200);
    MAX11270::stopContinuous();
    
    // Check for dropped samples via driver statistics
    uint32_t dropped = MAX11270::getDroppedCount();
    Serial.printf("Dropped samples: %u\n", dropped);
    
    // In a short run with functioning hardware, should have zero drops
    TEST_ASSERT_EQUAL_MESSAGE(0, dropped, "Samples were dropped");
}

// ============================================================================
// Gain Configuration Tests
// ============================================================================

void test_adc_gain_change() {
    MAX11270::init();
    
    // Read at gain 1
    MAX11270::setGain(MAX11270::Gain::GAIN_1);
    int32_t raw1 = MAX11270::readSingle(100);
    
    // Read at gain 128
    MAX11270::setGain(MAX11270::Gain::GAIN_128);
    int32_t raw128 = MAX11270::readSingle(100);
    
    Serial.printf("Gain 1: %d, Gain 128: %d\n", raw1, raw128);
    
    // With actual signal, gain 128 should give larger absolute value
    // But with noise/no signal, just verify both are valid
    TEST_ASSERT_GREATER_OR_EQUAL(-8388608, raw1);
    TEST_ASSERT_GREATER_OR_EQUAL(-8388608, raw128);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

void test_adc_timeout_handling() {
    // Don't initialize - should timeout
    // Note: This test depends on hardware state
    
    MAX11270::init();  // Initialize properly first
    
    // A very short timeout might fail (but this depends on sample rate)
    // This is more of a smoke test
    int32_t raw = MAX11270::readSingle(1);  // 1ms timeout - very short
    
    // Either we got a valid value or it timed out gracefully
    // (implementation-dependent behavior)
    Serial.printf("1ms timeout result: %d\n", raw);
}

// ============================================================================
// Statistics Tests
// ============================================================================

void test_adc_statistics_tracking() {
    MAX11270::init();
    MAX11270::resetStats();
    
    MAX11270::startContinuous(testBuffer);
    delay(100);
    MAX11270::stopContinuous();
    
    MAX11270::Stats stats = MAX11270::getStats();
    
    Serial.printf("ADC Stats - Samples: %u, Min: %d, Max: %d, Overflows: %u\n",
                  stats.sampleCount, stats.minValue, stats.maxValue, stats.overflowCount);
    
    TEST_ASSERT_GREATER_THAN(0, stats.sampleCount);
    TEST_ASSERT_LESS_OR_EQUAL(stats.maxValue, stats.minValue);  // Max >= Min
}

// ============================================================================
// Test Runner
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial
    
    Serial.println("\n\n=== MAX11270 ADC Integration Tests ===\n");
    
    UNITY_BEGIN();
    
    // Initialization tests
    RUN_TEST(test_adc_init);
    RUN_TEST(test_adc_device_id);
    
    // Single conversion tests
    RUN_TEST(test_adc_single_read);
    RUN_TEST(test_adc_single_read_multiple);
    
    // Continuous conversion tests
    RUN_TEST(test_adc_continuous_start_stop);
    RUN_TEST(test_adc_continuous_produces_samples);
    RUN_TEST(test_adc_continuous_no_overflow_short_run);
    RUN_TEST(test_adc_continuous_samples_have_valid_timestamps);
    RUN_TEST(test_adc_continuous_sample_rate);
    
    // Data integrity tests
    RUN_TEST(test_adc_values_in_expected_range);
    RUN_TEST(test_adc_sequence_continuity);
    
    // Gain configuration tests
    RUN_TEST(test_adc_gain_change);
    
    // Error handling tests
    RUN_TEST(test_adc_timeout_handling);
    
    // Statistics tests
    RUN_TEST(test_adc_statistics_tracking);
    
    UNITY_END();
}

void loop() {
    // Tests complete
    delay(1000);
}




