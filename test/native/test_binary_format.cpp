/**
 * @file test_binary_format.cpp
 * @brief Unit tests for Binary Log Format structures
 * 
 * Tests structure sizes, packing, validation, and utility functions.
 * These tests ensure binary format compatibility with parsers.
 */

#include <unity.h>
#include "logging/binary_format.h"
#include <cstring>

using namespace BinaryFormat;

void setUp() {}
void tearDown() {}

// ============================================================================
// Structure Size Tests (Critical for binary compatibility)
// ============================================================================

void test_file_header_size() {
    TEST_ASSERT_EQUAL(64, sizeof(FileHeader));
    TEST_ASSERT_EQUAL(64, HEADER_SIZE);
}

void test_adc_record_size() {
    TEST_ASSERT_EQUAL(12, sizeof(ADCRecord));
    TEST_ASSERT_EQUAL(12, ADCRecord::SIZE);
}

void test_imu_record_size() {
    TEST_ASSERT_EQUAL(16, sizeof(IMURecord));
    TEST_ASSERT_EQUAL(16, IMURecord::SIZE);
}

void test_tagged_adc_record_size() {
    TEST_ASSERT_EQUAL(13, sizeof(TaggedADCRecord));
    TEST_ASSERT_EQUAL(13, TaggedADCRecord::SIZE);
}

void test_tagged_imu_record_size() {
    TEST_ASSERT_EQUAL(17, sizeof(TaggedIMURecord));
    TEST_ASSERT_EQUAL(17, TaggedIMURecord::SIZE);
}

void test_event_record_min_size() {
    TEST_ASSERT_EQUAL(8, sizeof(EventRecord));
    TEST_ASSERT_EQUAL(8, EventRecord::MIN_SIZE);
}

void test_end_record_size() {
    TEST_ASSERT_EQUAL(9, sizeof(EndRecord));
    TEST_ASSERT_EQUAL(9, EndRecord::SIZE);
}

void test_file_footer_size() {
    TEST_ASSERT_EQUAL(32, sizeof(FileFooter));
    TEST_ASSERT_EQUAL(32, FileFooter::SIZE);
}

// ============================================================================
// Magic Number Tests
// ============================================================================

void test_file_magic_value() {
    // "LCLG" in ASCII = 0x4C 0x43 0x4C 0x47
    // Little-endian: 0x474C434C
    TEST_ASSERT_EQUAL_HEX32(0x474C434C, FILE_MAGIC);
}

void test_footer_magic_value() {
    TEST_ASSERT_EQUAL_HEX32(0xF007F007, FOOTER_MAGIC);
}

void test_format_version() {
    TEST_ASSERT_EQUAL(1, FORMAT_VERSION);
}

// ============================================================================
// FileHeader Tests
// ============================================================================

void test_file_header_init() {
    FileHeader header;
    header.init();
    
    TEST_ASSERT_EQUAL_HEX32(FILE_MAGIC, header.magic);
    TEST_ASSERT_EQUAL(FORMAT_VERSION, header.version);
    TEST_ASSERT_EQUAL(HEADER_SIZE, header.headerSize);
    TEST_ASSERT_EQUAL(64000, header.adcSampleRateHz);
    TEST_ASSERT_EQUAL(1000, header.imuSampleRateHz);
    TEST_ASSERT_EQUAL(0, header.startTimestampUs);
    TEST_ASSERT_EQUAL(0, header.flags);
    TEST_ASSERT_EQUAL(1, header.adcGain);
    TEST_ASSERT_EQUAL(24, header.adcBits);
}

void test_file_header_valid_after_init() {
    FileHeader header;
    header.init();
    
    TEST_ASSERT_TRUE(header.isValid());
}

void test_file_header_invalid_magic() {
    FileHeader header;
    header.init();
    header.magic = 0x12345678;
    
    TEST_ASSERT_FALSE(header.isValid());
}

void test_file_header_invalid_version() {
    FileHeader header;
    header.init();
    header.version = 99;
    
    TEST_ASSERT_FALSE(header.isValid());
}

void test_file_header_invalid_size() {
    FileHeader header;
    header.init();
    header.headerSize = 128;
    
    TEST_ASSERT_FALSE(header.isValid());
}

void test_file_header_loadcell_id_storage() {
    FileHeader header;
    header.init();
    
    const char* testId = "TC023L0-000025";
    strncpy(header.loadcellId, testId, sizeof(header.loadcellId) - 1);
    header.loadcellId[sizeof(header.loadcellId) - 1] = '\0';
    
    TEST_ASSERT_EQUAL_STRING(testId, header.loadcellId);
}

void test_file_header_loadcell_id_max_length() {
    FileHeader header;
    header.init();
    
    // Test that we can store 31 characters + null terminator
    const char* longId = "1234567890123456789012345678901";  // 31 chars
    TEST_ASSERT_EQUAL(31, strlen(longId));
    
    strncpy(header.loadcellId, longId, sizeof(header.loadcellId) - 1);
    header.loadcellId[sizeof(header.loadcellId) - 1] = '\0';
    
    TEST_ASSERT_EQUAL_STRING(longId, header.loadcellId);
}

// ============================================================================
// FileFooter Tests
// ============================================================================

void test_file_footer_init() {
    FileFooter footer;
    footer.init();
    
    TEST_ASSERT_EQUAL_HEX32(FOOTER_MAGIC, footer.magic);
    TEST_ASSERT_EQUAL(0, footer.totalAdcSamples);
    TEST_ASSERT_EQUAL(0, footer.totalImuSamples);
    TEST_ASSERT_EQUAL(0, footer.droppedSamples);
    TEST_ASSERT_EQUAL(0, footer.endTimestampUs);
    TEST_ASSERT_EQUAL(0, footer.crc32);
}

void test_file_footer_valid_after_init() {
    FileFooter footer;
    footer.init();
    
    TEST_ASSERT_TRUE(footer.isValid());
}

void test_file_footer_invalid_magic() {
    FileFooter footer;
    footer.init();
    footer.magic = 0xDEADBEEF;
    
    TEST_ASSERT_FALSE(footer.isValid());
}

void test_file_footer_large_sample_counts() {
    FileFooter footer;
    footer.init();
    
    // Test 64-bit sample counts for long sessions
    footer.totalAdcSamples = 64000ULL * 3600 * 24;  // 24 hours at 64ksps
    footer.totalImuSamples = 1000ULL * 3600 * 24;   // 24 hours at 1ksps
    
    TEST_ASSERT_EQUAL_UINT64(5529600000ULL, footer.totalAdcSamples);
    TEST_ASSERT_EQUAL_UINT64(86400000ULL, footer.totalImuSamples);
}

// ============================================================================
// Record Type Tests
// ============================================================================

void test_record_type_values() {
    TEST_ASSERT_EQUAL(0x01, static_cast<uint8_t>(RecordType::ADC));
    TEST_ASSERT_EQUAL(0x02, static_cast<uint8_t>(RecordType::IMU));
    TEST_ASSERT_EQUAL(0x10, static_cast<uint8_t>(RecordType::Event));
    TEST_ASSERT_EQUAL(0x20, static_cast<uint8_t>(RecordType::Comment));
    TEST_ASSERT_EQUAL(0xFF, static_cast<uint8_t>(RecordType::End));
}

// ============================================================================
// ADC Record Tests
// ============================================================================

void test_adc_record_packing() {
    ADCRecord record;
    record.timestampOffsetUs = 0x12345678;
    record.rawAdc = 0x00ABCDEF;
    record.sequenceNum = 0xFEDCBA98;
    
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&record);
    
    // Verify little-endian byte order
    // timestampOffsetUs at offset 0
    TEST_ASSERT_EQUAL_HEX8(0x78, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x56, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x34, bytes[2]);
    TEST_ASSERT_EQUAL_HEX8(0x12, bytes[3]);
    
    // rawAdc at offset 4
    TEST_ASSERT_EQUAL_HEX8(0xEF, bytes[4]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, bytes[5]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, bytes[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, bytes[7]);
    
    // sequenceNum at offset 8
    TEST_ASSERT_EQUAL_HEX8(0x98, bytes[8]);
    TEST_ASSERT_EQUAL_HEX8(0xBA, bytes[9]);
    TEST_ASSERT_EQUAL_HEX8(0xDC, bytes[10]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, bytes[11]);
}

void test_adc_record_negative_value() {
    ADCRecord record;
    record.rawAdc = -1;  // Should be 0xFFFFFFFF in memory
    
    TEST_ASSERT_EQUAL(-1, record.rawAdc);
}

void test_adc_record_24bit_range() {
    ADCRecord record;
    
    // 24-bit max positive: 0x7FFFFF = 8388607
    record.rawAdc = 8388607;
    TEST_ASSERT_EQUAL(8388607, record.rawAdc);
    
    // 24-bit min negative: 0xFF800000 = -8388608
    record.rawAdc = -8388608;
    TEST_ASSERT_EQUAL(-8388608, record.rawAdc);
}

// ============================================================================
// IMU Record Tests
// ============================================================================

void test_imu_record_packing() {
    IMURecord record;
    record.timestampOffsetUs = 1000000;
    record.accelX = 1000;
    record.accelY = -2000;
    record.accelZ = 16384;  // 1g at 2g scale
    record.gyroX = 0;
    record.gyroY = 100;
    record.gyroZ = -100;
    
    // Verify we can read back
    TEST_ASSERT_EQUAL(1000000, record.timestampOffsetUs);
    TEST_ASSERT_EQUAL(1000, record.accelX);
    TEST_ASSERT_EQUAL(-2000, record.accelY);
    TEST_ASSERT_EQUAL(16384, record.accelZ);
    TEST_ASSERT_EQUAL(0, record.gyroX);
    TEST_ASSERT_EQUAL(100, record.gyroY);
    TEST_ASSERT_EQUAL(-100, record.gyroZ);
}

void test_imu_record_16bit_range() {
    IMURecord record;
    
    record.accelX = INT16_MAX;  // 32767
    record.accelY = INT16_MIN;  // -32768
    
    TEST_ASSERT_EQUAL(32767, record.accelX);
    TEST_ASSERT_EQUAL(-32768, record.accelY);
}

// ============================================================================
// Tagged Record Tests
// ============================================================================

void test_tagged_adc_record() {
    TaggedADCRecord tagged;
    tagged.type = static_cast<uint8_t>(RecordType::ADC);
    tagged.record.timestampOffsetUs = 1000;
    tagged.record.rawAdc = 12345;
    tagged.record.sequenceNum = 1;
    
    TEST_ASSERT_EQUAL(0x01, tagged.type);
    TEST_ASSERT_EQUAL(1000, tagged.record.timestampOffsetUs);
}

void test_tagged_imu_record() {
    TaggedIMURecord tagged;
    tagged.type = static_cast<uint8_t>(RecordType::IMU);
    tagged.record.timestampOffsetUs = 2000;
    tagged.record.accelX = 100;
    
    TEST_ASSERT_EQUAL(0x02, tagged.type);
    TEST_ASSERT_EQUAL(2000, tagged.record.timestampOffsetUs);
}

// ============================================================================
// Event Code Tests
// ============================================================================

void test_event_codes() {
    TEST_ASSERT_EQUAL(0x0001, EventCode::SessionStart);
    TEST_ASSERT_EQUAL(0x0002, EventCode::SessionEnd);
    TEST_ASSERT_EQUAL(0x0010, EventCode::ButtonPress);
    TEST_ASSERT_EQUAL(0x0020, EventCode::Overflow);
    TEST_ASSERT_EQUAL(0x00F0, EventCode::Checkpoint);
    TEST_ASSERT_EQUAL(0x00F5, EventCode::Recovery);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

void test_calculate_data_rate_untagged() {
    // ADC at 64 ksps = 64000 * 12 = 768000 bytes/sec
    // IMU at 1 ksps = 1000 * 16 = 16000 bytes/sec
    // Total = 784000 bytes/sec
    
    uint32_t rate = calculateDataRate(64000, 1000, false);
    TEST_ASSERT_EQUAL(784000, rate);
}

void test_calculate_data_rate_tagged() {
    // ADC at 64 ksps = 64000 * 13 = 832000 bytes/sec
    // IMU at 1 ksps = 1000 * 17 = 17000 bytes/sec
    // Total = 849000 bytes/sec
    
    uint32_t rate = calculateDataRate(64000, 1000, true);
    TEST_ASSERT_EQUAL(849000, rate);
}

void test_calculate_data_rate_adc_only() {
    uint32_t rate = calculateDataRate(64000, 0, false);
    TEST_ASSERT_EQUAL(768000, rate);  // 64000 * 12
}

void test_calculate_data_rate_imu_only() {
    uint32_t rate = calculateDataRate(0, 1000, false);
    TEST_ASSERT_EQUAL(16000, rate);  // 1000 * 16
}

void test_estimate_file_size() {
    // 1 minute at 64ksps ADC + 1ksps IMU
    uint64_t size = estimateFileSize(64000, 1000, 60);
    
    // Header (64) + 60 seconds * 784000 bytes/sec
    uint64_t expected = 64 + (60ULL * 784000);
    TEST_ASSERT_EQUAL_UINT64(expected, size);
}

void test_estimate_file_size_1_hour() {
    // 1 hour session
    uint64_t size = estimateFileSize(64000, 1000, 3600);
    
    // Should be about 2.8 GB
    TEST_ASSERT_GREATER_THAN(2800000000ULL, size);
    TEST_ASSERT_LESS_THAN(2900000000ULL, size);
}

// ============================================================================
// Binary Compatibility Tests (ensure we can create files parseable by tools)
// ============================================================================

void test_header_field_offsets() {
    FileHeader h;
    h.init();
    
    uint8_t* base = reinterpret_cast<uint8_t*>(&h);
    
    // Verify field offsets match documented format
    TEST_ASSERT_EQUAL(0, offsetof(FileHeader, magic));
    TEST_ASSERT_EQUAL(4, offsetof(FileHeader, version));
    TEST_ASSERT_EQUAL(6, offsetof(FileHeader, headerSize));
    TEST_ASSERT_EQUAL(8, offsetof(FileHeader, adcSampleRateHz));
    TEST_ASSERT_EQUAL(12, offsetof(FileHeader, imuSampleRateHz));
    TEST_ASSERT_EQUAL(16, offsetof(FileHeader, startTimestampUs));
    TEST_ASSERT_EQUAL(24, offsetof(FileHeader, loadcellId));
    TEST_ASSERT_EQUAL(56, offsetof(FileHeader, flags));
}

void test_footer_field_offsets() {
    TEST_ASSERT_EQUAL(0, offsetof(FileFooter, magic));
    TEST_ASSERT_EQUAL(4, offsetof(FileFooter, totalAdcSamples));
    TEST_ASSERT_EQUAL(12, offsetof(FileFooter, totalImuSamples));
    TEST_ASSERT_EQUAL(20, offsetof(FileFooter, droppedSamples));
    TEST_ASSERT_EQUAL(24, offsetof(FileFooter, endTimestampUs));
    TEST_ASSERT_EQUAL(28, offsetof(FileFooter, crc32));
}

// ============================================================================
// Test Runner
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // Structure size tests
    RUN_TEST(test_file_header_size);
    RUN_TEST(test_adc_record_size);
    RUN_TEST(test_imu_record_size);
    RUN_TEST(test_tagged_adc_record_size);
    RUN_TEST(test_tagged_imu_record_size);
    RUN_TEST(test_event_record_min_size);
    RUN_TEST(test_end_record_size);
    RUN_TEST(test_file_footer_size);
    
    // Magic number tests
    RUN_TEST(test_file_magic_value);
    RUN_TEST(test_footer_magic_value);
    RUN_TEST(test_format_version);
    
    // FileHeader tests
    RUN_TEST(test_file_header_init);
    RUN_TEST(test_file_header_valid_after_init);
    RUN_TEST(test_file_header_invalid_magic);
    RUN_TEST(test_file_header_invalid_version);
    RUN_TEST(test_file_header_invalid_size);
    RUN_TEST(test_file_header_loadcell_id_storage);
    RUN_TEST(test_file_header_loadcell_id_max_length);
    
    // FileFooter tests
    RUN_TEST(test_file_footer_init);
    RUN_TEST(test_file_footer_valid_after_init);
    RUN_TEST(test_file_footer_invalid_magic);
    RUN_TEST(test_file_footer_large_sample_counts);
    
    // Record type tests
    RUN_TEST(test_record_type_values);
    
    // ADC record tests
    RUN_TEST(test_adc_record_packing);
    RUN_TEST(test_adc_record_negative_value);
    RUN_TEST(test_adc_record_24bit_range);
    
    // IMU record tests
    RUN_TEST(test_imu_record_packing);
    RUN_TEST(test_imu_record_16bit_range);
    
    // Tagged record tests
    RUN_TEST(test_tagged_adc_record);
    RUN_TEST(test_tagged_imu_record);
    
    // Event code tests
    RUN_TEST(test_event_codes);
    
    // Utility function tests
    RUN_TEST(test_calculate_data_rate_untagged);
    RUN_TEST(test_calculate_data_rate_tagged);
    RUN_TEST(test_calculate_data_rate_adc_only);
    RUN_TEST(test_calculate_data_rate_imu_only);
    RUN_TEST(test_estimate_file_size);
    RUN_TEST(test_estimate_file_size_1_hour);
    
    // Binary compatibility tests
    RUN_TEST(test_header_field_offsets);
    RUN_TEST(test_footer_field_offsets);
    
    return UNITY_END();
}




