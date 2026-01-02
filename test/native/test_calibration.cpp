/**
 * @file test_calibration.cpp
 * @brief Unit tests for Calibration Types and Interpolation
 * 
 * Tests the calibration data structures and interpolation logic.
 * These tests use the types directly without NVS storage.
 */

#include <unity.h>
#include "calibration/loadcell_types.h"
#include <cmath>
#include <cstring>

using namespace Calibration;

void setUp() {}
void tearDown() {}

// ============================================================================
// Test Helper: Linear Interpolation Function
// ============================================================================

/**
 * @brief Standalone interpolation function for testing
 * 
 * This mirrors the logic in calibration_interp.cpp without NVS dependencies.
 */
float testInterpolate(float uV, const CalibrationPoint* points, uint8_t numPoints) {
    if (numPoints < 2) {
        return 0.0f;  // Invalid
    }
    
    // Find bracketing points (assumes points are sorted by output_uV)
    int lowerIdx = -1, upperIdx = -1;
    
    for (int i = 0; i < numPoints - 1; i++) {
        if (uV >= points[i].output_uV && uV <= points[i + 1].output_uV) {
            lowerIdx = i;
            upperIdx = i + 1;
            break;
        }
    }
    
    // Handle extrapolation
    if (lowerIdx < 0) {
        if (uV < points[0].output_uV) {
            // Below range - extrapolate from first two points
            lowerIdx = 0;
            upperIdx = 1;
        } else {
            // Above range - extrapolate from last two points
            lowerIdx = numPoints - 2;
            upperIdx = numPoints - 1;
        }
    }
    
    // Linear interpolation
    float uV_a = points[lowerIdx].output_uV;
    float uV_b = points[upperIdx].output_uV;
    float kg_a = points[lowerIdx].load_kg;
    float kg_b = points[upperIdx].load_kg;
    
    // Handle edge case of identical points
    if (fabsf(uV_b - uV_a) < 0.001f) {
        return (kg_a + kg_b) / 2.0f;
    }
    
    return kg_a + (uV - uV_a) * (kg_b - kg_a) / (uV_b - uV_a);
}

// ============================================================================
// CalibrationPoint Tests
// ============================================================================

void test_calibration_point_initialization() {
    CalibrationPoint point = {100.0f, 5000.0f};
    
    TEST_ASSERT_EQUAL_FLOAT(100.0f, point.load_kg);
    TEST_ASSERT_EQUAL_FLOAT(5000.0f, point.output_uV);
}

void test_calibration_point_comparison() {
    CalibrationPoint p1 = {100.0f, 5000.0f};
    CalibrationPoint p2 = {200.0f, 10000.0f};
    CalibrationPoint p3 = {50.0f, 2500.0f};
    
    // Should compare by output_uV, not load_kg
    TEST_ASSERT_TRUE(p3 < p1);
    TEST_ASSERT_TRUE(p1 < p2);
    TEST_ASSERT_FALSE(p2 < p1);
}

// ============================================================================
// LoadcellCalibration Tests
// ============================================================================

void test_loadcell_calibration_init() {
    LoadcellCalibration cal;
    cal.init();
    
    TEST_ASSERT_EQUAL_STRING("", cal.id);
    TEST_ASSERT_EQUAL_STRING("", cal.model);
    TEST_ASSERT_EQUAL_STRING("", cal.serial);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal.capacity_kg);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, cal.excitation_V);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, cal.sensitivity_mVV);
    TEST_ASSERT_EQUAL(0, cal.numPoints);
}

void test_loadcell_calibration_generate_id() {
    LoadcellCalibration cal;
    cal.init();
    
    strncpy(cal.model, "TC023L0", sizeof(cal.model));
    strncpy(cal.serial, "000025", sizeof(cal.serial));
    cal.generateId();
    
    TEST_ASSERT_EQUAL_STRING("TC023L0-000025", cal.id);
}

void test_loadcell_calibration_add_point() {
    LoadcellCalibration cal;
    cal.init();
    
    TEST_ASSERT_TRUE(cal.addPoint(0.0f, 0.0f));
    TEST_ASSERT_EQUAL(1, cal.numPoints);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal.points[0].load_kg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal.points[0].output_uV);
    
    TEST_ASSERT_TRUE(cal.addPoint(1000.0f, 20000.0f));
    TEST_ASSERT_EQUAL(2, cal.numPoints);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, cal.points[1].load_kg);
}

void test_loadcell_calibration_max_points() {
    LoadcellCalibration cal;
    cal.init();
    
    // Add maximum points
    for (int i = 0; i < MAX_CALIBRATION_POINTS; i++) {
        TEST_ASSERT_TRUE(cal.addPoint((float)i * 100.0f, (float)i * 1000.0f));
    }
    
    TEST_ASSERT_EQUAL(MAX_CALIBRATION_POINTS, cal.numPoints);
    
    // Adding one more should fail
    TEST_ASSERT_FALSE(cal.addPoint(9999.0f, 99999.0f));
    TEST_ASSERT_EQUAL(MAX_CALIBRATION_POINTS, cal.numPoints);
}

void test_loadcell_calibration_sort_points() {
    LoadcellCalibration cal;
    cal.init();
    
    // Add points out of order (by output_uV)
    cal.addPoint(500.0f, 10000.0f);  // Should be third
    cal.addPoint(0.0f, 0.0f);        // Should be first
    cal.addPoint(250.0f, 5000.0f);   // Should be second
    
    cal.sortPoints();
    
    // Verify sorted by output_uV
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal.points[0].output_uV);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal.points[0].load_kg);
    
    TEST_ASSERT_EQUAL_FLOAT(5000.0f, cal.points[1].output_uV);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, cal.points[1].load_kg);
    
    TEST_ASSERT_EQUAL_FLOAT(10000.0f, cal.points[2].output_uV);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, cal.points[2].load_kg);
}

void test_loadcell_calibration_is_valid() {
    LoadcellCalibration cal;
    cal.init();
    
    // Empty calibration is invalid
    TEST_ASSERT_FALSE(cal.isValid());
    
    // With ID but no points - invalid
    strncpy(cal.id, "TEST", sizeof(cal.id));
    TEST_ASSERT_FALSE(cal.isValid());
    
    // With ID and 1 point - invalid (need at least 2)
    cal.addPoint(0.0f, 0.0f);
    TEST_ASSERT_FALSE(cal.isValid());
    
    // With ID and 2 points but no capacity - invalid
    cal.addPoint(1000.0f, 20000.0f);
    TEST_ASSERT_FALSE(cal.isValid());
    
    // With everything - valid
    cal.capacity_kg = 1000.0f;
    TEST_ASSERT_TRUE(cal.isValid());
}

// ============================================================================
// Linear Interpolation Tests
// ============================================================================

void test_interpolation_at_calibration_points() {
    // Create a simple 2-point calibration
    CalibrationPoint points[2] = {
        {0.0f, 0.0f},        // 0 kg = 0 uV
        {1000.0f, 20000.0f}  // 1000 kg = 20000 uV
    };
    
    // Interpolation at exact calibration points
    float kg0 = testInterpolate(0.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, kg0);
    
    float kg1000 = testInterpolate(20000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, kg1000);
}

void test_interpolation_between_points() {
    CalibrationPoint points[2] = {
        {0.0f, 0.0f},
        {1000.0f, 20000.0f}
    };
    
    // 50% between points
    float kg500 = testInterpolate(10000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 500.0f, kg500);
    
    // 25% between points
    float kg250 = testInterpolate(5000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 250.0f, kg250);
    
    // 75% between points
    float kg750 = testInterpolate(15000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 750.0f, kg750);
}

void test_interpolation_multipoint() {
    // Non-linear calibration curve (3 points)
    CalibrationPoint points[3] = {
        {0.0f, 0.0f},
        {500.0f, 9000.0f},   // Slightly non-linear
        {1000.0f, 20000.0f}
    };
    
    // Between first two points
    float kg250 = testInterpolate(4500.0f, points, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 250.0f, kg250);
    
    // At middle point
    float kg500 = testInterpolate(9000.0f, points, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 500.0f, kg500);
    
    // Between last two points (different slope)
    // 9000 -> 20000 = 11000 range for 500 kg
    // At 14500 uV, should be halfway = 750 kg
    float kg750 = testInterpolate(14500.0f, points, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 750.0f, kg750);
}

void test_extrapolation_below_range() {
    CalibrationPoint points[2] = {
        {0.0f, 1000.0f},     // Note: doesn't start at 0 uV
        {1000.0f, 21000.0f}
    };
    
    // Below calibration range - should extrapolate
    float kgNeg = testInterpolate(0.0f, points, 2);
    // 1000 uV = 0 kg, so 0 uV should be -50 kg
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -50.0f, kgNeg);
}

void test_extrapolation_above_range() {
    CalibrationPoint points[2] = {
        {0.0f, 0.0f},
        {1000.0f, 20000.0f}
    };
    
    // Above calibration range
    float kgAbove = testInterpolate(25000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1250.0f, kgAbove);
}

void test_interpolation_with_zero_offset() {
    // Test with zero balance offset
    CalibrationPoint points[2] = {
        {0.0f, 500.0f},      // 500 uV at no load (zero offset)
        {1000.0f, 20500.0f}  // 20500 uV at full scale
    };
    
    // At zero balance output
    float kg0 = testInterpolate(500.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, kg0);
    
    // At full scale
    float kg1000 = testInterpolate(20500.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, kg1000);
    
    // At midpoint
    float kg500 = testInterpolate(10500.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 500.0f, kg500);
}

void test_interpolation_negative_loads() {
    // Compression loadcell (negative loads)
    CalibrationPoint points[3] = {
        {-500.0f, -10000.0f},
        {0.0f, 0.0f},
        {500.0f, 10000.0f}
    };
    
    // Negative load
    float kgNeg = testInterpolate(-5000.0f, points, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -250.0f, kgNeg);
    
    // Zero load
    float kg0 = testInterpolate(0.0f, points, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, kg0);
    
    // Positive load
    float kgPos = testInterpolate(5000.0f, points, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 250.0f, kgPos);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

void test_interpolation_identical_points_handled() {
    // Edge case: two points with same output voltage (shouldn't happen but handle gracefully)
    CalibrationPoint points[2] = {
        {100.0f, 5000.0f},
        {200.0f, 5000.0f}  // Same uV, different load
    };
    
    // Should return average of the two loads
    float kg = testInterpolate(5000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 150.0f, kg);
}

void test_interpolation_single_point_returns_zero() {
    CalibrationPoint points[1] = {{100.0f, 5000.0f}};
    
    // Single point is invalid for interpolation
    float kg = testInterpolate(5000.0f, points, 1);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, kg);
}

void test_interpolation_large_values() {
    // High-capacity loadcell
    CalibrationPoint points[2] = {
        {0.0f, 0.0f},
        {50000.0f, 40000.0f}  // 50 ton capacity
    };
    
    float kg = testInterpolate(20000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25000.0f, kg);
}

void test_interpolation_small_values() {
    // Precision loadcell
    CalibrationPoint points[2] = {
        {0.0f, 0.0f},
        {0.1f, 2000.0f}  // 100 gram capacity
    };
    
    float kg = testInterpolate(1000.0f, points, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.05f, kg);
}

// ============================================================================
// NVS Key Generation Tests
// ============================================================================

void test_generate_nvs_key_short_id() {
    char key[16];
    generateNvsKey("TEST", key, sizeof(key));
    
    TEST_ASSERT_EQUAL_STRING("lc_TEST", key);
}

void test_generate_nvs_key_long_id() {
    char key[16];
    generateNvsKey("TC023L0-000025-EXTRA", key, sizeof(key));
    
    // Should be truncated: "lc_" + first 12 chars
    TEST_ASSERT_EQUAL_STRING("lc_TC023L0-0000", key);
}

void test_generate_nvs_key_special_chars() {
    char key[16];
    generateNvsKey("TEST/ID:123", key, sizeof(key));
    
    // Special chars should be replaced with underscore
    TEST_ASSERT_EQUAL_STRING("lc_TEST_ID_123", key);
}

// ============================================================================
// Test Runner
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // CalibrationPoint tests
    RUN_TEST(test_calibration_point_initialization);
    RUN_TEST(test_calibration_point_comparison);
    
    // LoadcellCalibration tests
    RUN_TEST(test_loadcell_calibration_init);
    RUN_TEST(test_loadcell_calibration_generate_id);
    RUN_TEST(test_loadcell_calibration_add_point);
    RUN_TEST(test_loadcell_calibration_max_points);
    RUN_TEST(test_loadcell_calibration_sort_points);
    RUN_TEST(test_loadcell_calibration_is_valid);
    
    // Linear interpolation tests
    RUN_TEST(test_interpolation_at_calibration_points);
    RUN_TEST(test_interpolation_between_points);
    RUN_TEST(test_interpolation_multipoint);
    RUN_TEST(test_extrapolation_below_range);
    RUN_TEST(test_extrapolation_above_range);
    RUN_TEST(test_interpolation_with_zero_offset);
    RUN_TEST(test_interpolation_negative_loads);
    
    // Edge case tests
    RUN_TEST(test_interpolation_identical_points_handled);
    RUN_TEST(test_interpolation_single_point_returns_zero);
    RUN_TEST(test_interpolation_large_values);
    RUN_TEST(test_interpolation_small_values);
    
    // NVS key generation tests
    RUN_TEST(test_generate_nvs_key_short_id);
    RUN_TEST(test_generate_nvs_key_long_id);
    RUN_TEST(test_generate_nvs_key_special_chars);
    
    return UNITY_END();
}




