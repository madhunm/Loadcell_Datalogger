/**
 * @file test_sd_integration.cpp
 * @brief Integration tests for SD card manager
 * 
 * These tests run on the ESP32-S3 with SD card inserted.
 * Requires: SD card properly wired via SDMMC interface.
 * 
 * WARNING: These tests create and delete files on the SD card!
 * 
 * Tests:
 * - SD card mounting
 * - File create/write/read/delete
 * - Large file handling
 * - Performance benchmarks
 */

#include <unity.h>
#include <Arduino.h>
#include "drivers/sd_manager.h"
#include <FS.h>
#include <SD_MMC.h>

// Test file names
static const char* TEST_FILE = "/test_file.txt";
static const char* TEST_BIN_FILE = "/test_data.bin";

void setUp() {
    // Ensure SD is mounted before each test
    if (!SDManager::isMounted()) {
        SDManager::init();
    }
}

void tearDown() {
    // Clean up test files
    if (SDManager::isMounted()) {
        SDManager::remove(TEST_FILE);
        SDManager::remove(TEST_BIN_FILE);
    }
}

// ============================================================================
// Mount/Unmount Tests
// ============================================================================

void test_sd_init() {
    bool result = SDManager::init();
    TEST_ASSERT_TRUE_MESSAGE(result, "SD card init failed - check card and wiring");
}

void test_sd_is_mounted() {
    SDManager::init();
    TEST_ASSERT_TRUE(SDManager::isMounted());
}

void test_sd_card_info() {
    SDManager::init();
    
    SDManager::CardInfo info = SDManager::getCardInfo();
    
    Serial.printf("SD Card Info:\n");
    Serial.printf("  Type: %s\n", info.type);
    Serial.printf("  Size: %llu MB\n", info.totalBytes / (1024 * 1024));
    Serial.printf("  Used: %llu MB\n", info.usedBytes / (1024 * 1024));
    Serial.printf("  Free: %llu MB\n", (info.totalBytes - info.usedBytes) / (1024 * 1024));
    
    // Should have non-zero size
    TEST_ASSERT_GREATER_THAN(0, info.totalBytes);
}

// ============================================================================
// Basic File Operations
// ============================================================================

void test_sd_create_file() {
    bool result = SDManager::createFile(TEST_FILE);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(SDManager::exists(TEST_FILE));
}

void test_sd_write_file() {
    SDManager::createFile(TEST_FILE);
    
    const char* testData = "Hello, SD Card!";
    size_t written = SDManager::writeFile(TEST_FILE, (const uint8_t*)testData, strlen(testData));
    
    TEST_ASSERT_EQUAL(strlen(testData), written);
}

void test_sd_read_file() {
    const char* testData = "Test read data 12345";
    SDManager::createFile(TEST_FILE);
    SDManager::writeFile(TEST_FILE, (const uint8_t*)testData, strlen(testData));
    
    uint8_t buffer[64];
    size_t read = SDManager::readFile(TEST_FILE, buffer, sizeof(buffer));
    buffer[read] = '\0';
    
    TEST_ASSERT_EQUAL(strlen(testData), read);
    TEST_ASSERT_EQUAL_STRING(testData, (char*)buffer);
}

void test_sd_append_file() {
    SDManager::createFile(TEST_FILE);
    SDManager::writeFile(TEST_FILE, (const uint8_t*)"Part1", 5);
    SDManager::appendFile(TEST_FILE, (const uint8_t*)"Part2", 5);
    
    uint8_t buffer[64];
    size_t read = SDManager::readFile(TEST_FILE, buffer, sizeof(buffer));
    buffer[read] = '\0';
    
    TEST_ASSERT_EQUAL_STRING("Part1Part2", (char*)buffer);
}

void test_sd_delete_file() {
    SDManager::createFile(TEST_FILE);
    TEST_ASSERT_TRUE(SDManager::exists(TEST_FILE));
    
    bool result = SDManager::remove(TEST_FILE);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(SDManager::exists(TEST_FILE));
}

void test_sd_file_size() {
    const char* testData = "12345678901234567890";  // 20 bytes
    SDManager::createFile(TEST_FILE);
    SDManager::writeFile(TEST_FILE, (const uint8_t*)testData, strlen(testData));
    
    size_t size = SDManager::getFileSize(TEST_FILE);
    TEST_ASSERT_EQUAL(20, size);
}

// ============================================================================
// Binary File Tests
// ============================================================================

void test_sd_write_binary() {
    uint8_t testData[256];
    for (int i = 0; i < 256; i++) {
        testData[i] = (uint8_t)i;
    }
    
    SDManager::createFile(TEST_BIN_FILE);
    size_t written = SDManager::writeFile(TEST_BIN_FILE, testData, sizeof(testData));
    
    TEST_ASSERT_EQUAL(256, written);
}

void test_sd_read_binary() {
    // Write binary pattern
    uint8_t writeData[256];
    for (int i = 0; i < 256; i++) {
        writeData[i] = (uint8_t)i;
    }
    
    SDManager::createFile(TEST_BIN_FILE);
    SDManager::writeFile(TEST_BIN_FILE, writeData, sizeof(writeData));
    
    // Read back
    uint8_t readData[256];
    size_t read = SDManager::readFile(TEST_BIN_FILE, readData, sizeof(readData));
    
    TEST_ASSERT_EQUAL(256, read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(writeData, readData, 256);
}

// ============================================================================
// Large File Tests
// ============================================================================

void test_sd_large_file_write() {
    // Write 1MB file
    const size_t CHUNK_SIZE = 4096;
    const size_t TOTAL_SIZE = 1024 * 1024;  // 1MB
    
    uint8_t* buffer = (uint8_t*)malloc(CHUNK_SIZE);
    TEST_ASSERT_NOT_NULL(buffer);
    
    // Fill with pattern
    for (size_t i = 0; i < CHUNK_SIZE; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    SDManager::createFile(TEST_BIN_FILE);
    
    uint32_t startTime = millis();
    size_t totalWritten = 0;
    
    for (size_t i = 0; i < TOTAL_SIZE / CHUNK_SIZE; i++) {
        size_t written = SDManager::appendFile(TEST_BIN_FILE, buffer, CHUNK_SIZE);
        totalWritten += written;
        if (written != CHUNK_SIZE) break;
    }
    
    uint32_t elapsed = millis() - startTime;
    float speedMBps = (float)totalWritten / elapsed / 1000.0f;
    
    free(buffer);
    
    Serial.printf("Large file write: %u bytes in %u ms (%.2f MB/s)\n", 
                  totalWritten, elapsed, speedMBps);
    
    TEST_ASSERT_EQUAL(TOTAL_SIZE, totalWritten);
    
    // Should be at least 1 MB/s
    TEST_ASSERT_GREATER_THAN_MESSAGE(1.0f, speedMBps, 
        "Write speed too slow");
}

void test_sd_large_file_read() {
    // First create the file
    const size_t CHUNK_SIZE = 4096;
    const size_t TOTAL_SIZE = 1024 * 1024;
    
    uint8_t* buffer = (uint8_t*)malloc(CHUNK_SIZE);
    TEST_ASSERT_NOT_NULL(buffer);
    
    for (size_t i = 0; i < CHUNK_SIZE; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    SDManager::createFile(TEST_BIN_FILE);
    for (size_t i = 0; i < TOTAL_SIZE / CHUNK_SIZE; i++) {
        SDManager::appendFile(TEST_BIN_FILE, buffer, CHUNK_SIZE);
    }
    
    // Now read it back
    uint32_t startTime = millis();
    size_t totalRead = 0;
    
    File file = SD_MMC.open(TEST_BIN_FILE, FILE_READ);
    if (file) {
        while (file.available()) {
            size_t read = file.read(buffer, CHUNK_SIZE);
            totalRead += read;
        }
        file.close();
    }
    
    uint32_t elapsed = millis() - startTime;
    float speedMBps = (float)totalRead / elapsed / 1000.0f;
    
    free(buffer);
    
    Serial.printf("Large file read: %u bytes in %u ms (%.2f MB/s)\n", 
                  totalRead, elapsed, speedMBps);
    
    TEST_ASSERT_EQUAL(TOTAL_SIZE, totalRead);
    TEST_ASSERT_GREATER_THAN(1.0f, speedMBps);
}

// ============================================================================
// Directory Tests
// ============================================================================

void test_sd_list_directory() {
    // Create a few test files
    SDManager::createFile("/test1.txt");
    SDManager::createFile("/test2.txt");
    SDManager::createFile("/test3.txt");
    
    int fileCount = 0;
    SDManager::listDir("/", [&](const char* name, size_t size, bool isDir) {
        Serial.printf("  %s%s (%u bytes)\n", name, isDir ? "/" : "", size);
        fileCount++;
    });
    
    // Clean up
    SDManager::remove("/test1.txt");
    SDManager::remove("/test2.txt");
    SDManager::remove("/test3.txt");
    
    TEST_ASSERT_GREATER_OR_EQUAL(3, fileCount);
}

void test_sd_create_directory() {
    bool result = SDManager::mkdir("/test_dir");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(SDManager::exists("/test_dir"));
    
    // Create file in directory
    result = SDManager::createFile("/test_dir/nested.txt");
    TEST_ASSERT_TRUE(result);
    
    // Clean up
    SDManager::remove("/test_dir/nested.txt");
    SDManager::rmdir("/test_dir");
}

// ============================================================================
// Performance Tests
// ============================================================================

void test_sd_sequential_write_latency() {
    const size_t WRITE_SIZE = 512;
    const int NUM_WRITES = 100;
    
    uint8_t buffer[WRITE_SIZE];
    memset(buffer, 0xAA, WRITE_SIZE);
    
    uint32_t minLatency = UINT32_MAX;
    uint32_t maxLatency = 0;
    uint32_t totalLatency = 0;
    
    File file = SD_MMC.open(TEST_BIN_FILE, FILE_WRITE);
    TEST_ASSERT_TRUE(file);
    
    for (int i = 0; i < NUM_WRITES; i++) {
        uint32_t start = micros();
        file.write(buffer, WRITE_SIZE);
        file.flush();
        uint32_t latency = micros() - start;
        
        if (latency < minLatency) minLatency = latency;
        if (latency > maxLatency) maxLatency = latency;
        totalLatency += latency;
    }
    
    file.close();
    
    Serial.printf("Write latency (512B, %d writes):\n", NUM_WRITES);
    Serial.printf("  Min: %u us\n", minLatency);
    Serial.printf("  Max: %u us\n", maxLatency);
    Serial.printf("  Avg: %u us\n", totalLatency / NUM_WRITES);
    
    // Max latency should be under 100ms (SD card cluster allocation spikes)
    TEST_ASSERT_LESS_THAN_MESSAGE(100000, maxLatency, 
        "Max write latency too high");
}

void test_sd_sustained_write() {
    // Simulate logging: continuous 4KB writes
    const size_t WRITE_SIZE = 4096;
    const uint32_t DURATION_MS = 5000;  // 5 seconds
    
    uint8_t* buffer = (uint8_t*)malloc(WRITE_SIZE);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0x55, WRITE_SIZE);
    
    File file = SD_MMC.open(TEST_BIN_FILE, FILE_WRITE);
    TEST_ASSERT_TRUE(file);
    
    uint32_t startTime = millis();
    size_t totalWritten = 0;
    uint32_t maxLatency = 0;
    
    while (millis() - startTime < DURATION_MS) {
        uint32_t writeStart = micros();
        size_t written = file.write(buffer, WRITE_SIZE);
        uint32_t latency = micros() - writeStart;
        
        if (latency > maxLatency) maxLatency = latency;
        totalWritten += written;
        
        if (written != WRITE_SIZE) break;
    }
    
    file.close();
    free(buffer);
    
    uint32_t elapsed = millis() - startTime;
    float speedMBps = (float)totalWritten / elapsed / 1000.0f;
    
    Serial.printf("Sustained write: %u bytes in %u ms\n", totalWritten, elapsed);
    Serial.printf("  Speed: %.2f MB/s\n", speedMBps);
    Serial.printf("  Max latency: %u ms\n", maxLatency / 1000);
    
    // For logging at 784 KB/s, need at least 1 MB/s sustained
    TEST_ASSERT_GREATER_THAN_MESSAGE(0.8f, speedMBps, 
        "Sustained write speed too slow for logging");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

void test_sd_read_nonexistent_file() {
    uint8_t buffer[64];
    size_t read = SDManager::readFile("/nonexistent.txt", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(0, read);
}

void test_sd_file_exists_check() {
    TEST_ASSERT_FALSE(SDManager::exists("/definitely_not_here.xyz"));
    
    SDManager::createFile(TEST_FILE);
    TEST_ASSERT_TRUE(SDManager::exists(TEST_FILE));
}

// ============================================================================
// Test Runner
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n\n=== SD Card Integration Tests ===\n");
    Serial.println("WARNING: This test creates/deletes files on SD card!\n");
    
    UNITY_BEGIN();
    
    // Mount tests
    RUN_TEST(test_sd_init);
    RUN_TEST(test_sd_is_mounted);
    RUN_TEST(test_sd_card_info);
    
    // Basic file operations
    RUN_TEST(test_sd_create_file);
    RUN_TEST(test_sd_write_file);
    RUN_TEST(test_sd_read_file);
    RUN_TEST(test_sd_append_file);
    RUN_TEST(test_sd_delete_file);
    RUN_TEST(test_sd_file_size);
    
    // Binary file tests
    RUN_TEST(test_sd_write_binary);
    RUN_TEST(test_sd_read_binary);
    
    // Large file tests
    RUN_TEST(test_sd_large_file_write);
    RUN_TEST(test_sd_large_file_read);
    
    // Directory tests
    RUN_TEST(test_sd_list_directory);
    RUN_TEST(test_sd_create_directory);
    
    // Performance tests
    RUN_TEST(test_sd_sequential_write_latency);
    RUN_TEST(test_sd_sustained_write);
    
    // Error handling tests
    RUN_TEST(test_sd_read_nonexistent_file);
    RUN_TEST(test_sd_file_exists_check);
    
    UNITY_END();
    
    // Final cleanup
    SDManager::remove(TEST_FILE);
    SDManager::remove(TEST_BIN_FILE);
}

void loop() {
    delay(1000);
}




