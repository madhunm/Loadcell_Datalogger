/**
 * @file test_ring_buffer.cpp
 * @brief Unit tests for SPSC Ring Buffer
 * 
 * Tests the lock-free ring buffer implementation for:
 * - Basic push/pop operations
 * - Empty/full state detection
 * - Overflow counting
 * - Batch operations
 * - Statistics tracking
 */

#include <unity.h>
#include "logging/ring_buffer.h"

// Use a small buffer for testing
using TestBuffer = RingBuffer<16>;

// Test fixtures
static TestBuffer* buffer;

void setUp() {
    buffer = new TestBuffer();
}

void tearDown() {
    delete buffer;
    buffer = nullptr;
}

// ============================================================================
// Basic State Tests
// ============================================================================

void test_buffer_starts_empty() {
    TEST_ASSERT_TRUE(buffer->isEmpty());
    TEST_ASSERT_FALSE(buffer->isFull());
    TEST_ASSERT_EQUAL(0, buffer->available());
    TEST_ASSERT_EQUAL(15, buffer->capacity());  // N-1 for SPSC
}

void test_buffer_capacity_is_n_minus_1() {
    // SPSC ring buffer wastes one slot to distinguish empty from full
    TEST_ASSERT_EQUAL(15, buffer->capacity());
    TEST_ASSERT_EQUAL(15, buffer->freeSpace());
}

// ============================================================================
// Push/Pop Tests
// ============================================================================

void test_push_single_sample() {
    ADCSample sample = {12345, 1000};
    
    TEST_ASSERT_TRUE(buffer->push(sample));
    TEST_ASSERT_FALSE(buffer->isEmpty());
    TEST_ASSERT_EQUAL(1, buffer->available());
    TEST_ASSERT_EQUAL(14, buffer->freeSpace());
}

void test_pop_single_sample() {
    ADCSample in = {12345, 1000};
    ADCSample out = {0, 0};
    
    buffer->push(in);
    TEST_ASSERT_TRUE(buffer->pop(out));
    
    TEST_ASSERT_EQUAL(in.raw, out.raw);
    TEST_ASSERT_EQUAL(in.timestamp_us, out.timestamp_us);
    TEST_ASSERT_TRUE(buffer->isEmpty());
}

void test_pop_from_empty_returns_false() {
    ADCSample out;
    TEST_ASSERT_FALSE(buffer->pop(out));
}

void test_push_pop_fifo_order() {
    // Push several samples
    for (int i = 0; i < 5; i++) {
        ADCSample s = {i * 100, (uint32_t)(i * 1000)};
        TEST_ASSERT_TRUE(buffer->push(s));
    }
    
    // Pop should return in FIFO order
    for (int i = 0; i < 5; i++) {
        ADCSample out;
        TEST_ASSERT_TRUE(buffer->pop(out));
        TEST_ASSERT_EQUAL(i * 100, out.raw);
        TEST_ASSERT_EQUAL(i * 1000, out.timestamp_us);
    }
    
    TEST_ASSERT_TRUE(buffer->isEmpty());
}

void test_push_pop_interleaved() {
    ADCSample s, out;
    
    // Push 3, pop 2, push 2, pop 3
    s = {100, 1000}; buffer->push(s);
    s = {200, 2000}; buffer->push(s);
    s = {300, 3000}; buffer->push(s);
    
    buffer->pop(out); TEST_ASSERT_EQUAL(100, out.raw);
    buffer->pop(out); TEST_ASSERT_EQUAL(200, out.raw);
    
    s = {400, 4000}; buffer->push(s);
    s = {500, 5000}; buffer->push(s);
    
    buffer->pop(out); TEST_ASSERT_EQUAL(300, out.raw);
    buffer->pop(out); TEST_ASSERT_EQUAL(400, out.raw);
    buffer->pop(out); TEST_ASSERT_EQUAL(500, out.raw);
    
    TEST_ASSERT_TRUE(buffer->isEmpty());
}

// ============================================================================
// Full/Overflow Tests
// ============================================================================

void test_buffer_detects_full() {
    // Fill buffer to capacity (N-1 items)
    for (size_t i = 0; i < buffer->capacity(); i++) {
        ADCSample s = {(int32_t)i, i};
        TEST_ASSERT_TRUE(buffer->push(s));
    }
    
    TEST_ASSERT_TRUE(buffer->isFull());
    TEST_ASSERT_FALSE(buffer->isEmpty());
    TEST_ASSERT_EQUAL(buffer->capacity(), buffer->available());
}

void test_push_to_full_buffer_fails() {
    // Fill buffer
    for (size_t i = 0; i < buffer->capacity(); i++) {
        ADCSample s = {(int32_t)i, i};
        buffer->push(s);
    }
    
    // Next push should fail
    ADCSample s = {999, 999};
    TEST_ASSERT_FALSE(buffer->push(s));
}

void test_overflow_counter_increments() {
    // Fill buffer
    for (size_t i = 0; i < buffer->capacity(); i++) {
        ADCSample s = {(int32_t)i, i};
        buffer->push(s);
    }
    
    TEST_ASSERT_EQUAL(0, buffer->getOverflowCount());
    TEST_ASSERT_FALSE(buffer->hasOverflow());
    
    // Attempt to push more - should increment overflow
    ADCSample s = {999, 999};
    buffer->push(s);
    TEST_ASSERT_EQUAL(1, buffer->getOverflowCount());
    TEST_ASSERT_TRUE(buffer->hasOverflow());
    
    buffer->push(s);
    buffer->push(s);
    TEST_ASSERT_EQUAL(3, buffer->getOverflowCount());
}

void test_clear_overflow() {
    // Fill and overflow
    for (size_t i = 0; i <= buffer->capacity(); i++) {
        ADCSample s = {(int32_t)i, i};
        buffer->push(s);
    }
    
    TEST_ASSERT_TRUE(buffer->hasOverflow());
    
    buffer->clearOverflow();
    TEST_ASSERT_FALSE(buffer->hasOverflow());
    TEST_ASSERT_EQUAL(0, buffer->getOverflowCount());
}

// ============================================================================
// Batch Operation Tests
// ============================================================================

void test_pop_batch_all_available() {
    // Push 5 samples
    for (int i = 0; i < 5; i++) {
        ADCSample s = {i * 100, (uint32_t)(i * 1000)};
        buffer->push(s);
    }
    
    // Pop all in batch
    ADCSample out[10];
    size_t count = buffer->popBatch(out, 10);
    
    TEST_ASSERT_EQUAL(5, count);
    TEST_ASSERT_TRUE(buffer->isEmpty());
    
    // Verify order
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i * 100, out[i].raw);
    }
}

void test_pop_batch_partial() {
    // Push 10 samples
    for (int i = 0; i < 10; i++) {
        ADCSample s = {i * 100, (uint32_t)i};
        buffer->push(s);
    }
    
    // Pop only 5
    ADCSample out[5];
    size_t count = buffer->popBatch(out, 5);
    
    TEST_ASSERT_EQUAL(5, count);
    TEST_ASSERT_EQUAL(5, buffer->available());
    
    // Verify first batch
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i * 100, out[i].raw);
    }
    
    // Pop remaining
    count = buffer->popBatch(out, 5);
    TEST_ASSERT_EQUAL(5, count);
    TEST_ASSERT_TRUE(buffer->isEmpty());
    
    // Verify second batch continues sequence
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL((i + 5) * 100, out[i].raw);
    }
}

void test_pop_batch_empty_returns_zero() {
    ADCSample out[10];
    size_t count = buffer->popBatch(out, 10);
    TEST_ASSERT_EQUAL(0, count);
}

// ============================================================================
// Statistics Tests
// ============================================================================

void test_total_pushed_counter() {
    TEST_ASSERT_EQUAL(0, buffer->getTotalPushed());
    
    for (int i = 0; i < 5; i++) {
        ADCSample s = {i, (uint32_t)i};
        buffer->push(s);
    }
    
    TEST_ASSERT_EQUAL(5, buffer->getTotalPushed());
    
    // Pop doesn't affect total pushed
    ADCSample out;
    buffer->pop(out);
    TEST_ASSERT_EQUAL(5, buffer->getTotalPushed());
}

void test_reset_stats() {
    // Push and overflow
    for (size_t i = 0; i <= buffer->capacity() + 2; i++) {
        ADCSample s = {(int32_t)i, i};
        buffer->push(s);
    }
    
    TEST_ASSERT_GREATER_THAN(0, buffer->getTotalPushed());
    TEST_ASSERT_GREATER_THAN(0, buffer->getOverflowCount());
    
    buffer->resetStats();
    
    TEST_ASSERT_EQUAL(0, buffer->getTotalPushed());
    TEST_ASSERT_EQUAL(0, buffer->getOverflowCount());
}

// ============================================================================
// Reset Tests
// ============================================================================

void test_reset_clears_buffer() {
    // Add some samples
    for (int i = 0; i < 5; i++) {
        ADCSample s = {i, (uint32_t)i};
        buffer->push(s);
    }
    
    TEST_ASSERT_FALSE(buffer->isEmpty());
    
    buffer->reset();
    
    TEST_ASSERT_TRUE(buffer->isEmpty());
    TEST_ASSERT_EQUAL(0, buffer->available());
}

// ============================================================================
// Wraparound Tests
// ============================================================================

void test_wraparound_behavior() {
    // Fill and drain multiple times to ensure wraparound works
    for (int cycle = 0; cycle < 5; cycle++) {
        // Fill buffer
        for (size_t i = 0; i < buffer->capacity(); i++) {
            ADCSample s = {(int32_t)(cycle * 100 + i), i};
            TEST_ASSERT_TRUE(buffer->push(s));
        }
        
        // Verify full
        TEST_ASSERT_TRUE(buffer->isFull());
        
        // Drain buffer
        for (size_t i = 0; i < buffer->capacity(); i++) {
            ADCSample out;
            TEST_ASSERT_TRUE(buffer->pop(out));
            TEST_ASSERT_EQUAL(cycle * 100 + i, out.raw);
        }
        
        // Verify empty
        TEST_ASSERT_TRUE(buffer->isEmpty());
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

void test_negative_adc_values() {
    ADCSample s = {-8388608, 1000};  // Min 24-bit signed
    buffer->push(s);
    
    ADCSample out;
    buffer->pop(out);
    TEST_ASSERT_EQUAL(-8388608, out.raw);
}

void test_max_adc_values() {
    ADCSample s = {8388607, UINT32_MAX};  // Max 24-bit signed, max timestamp
    buffer->push(s);
    
    ADCSample out;
    buffer->pop(out);
    TEST_ASSERT_EQUAL(8388607, out.raw);
    TEST_ASSERT_EQUAL(UINT32_MAX, out.timestamp_us);
}

// ============================================================================
// Test Runner
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // Basic state tests
    RUN_TEST(test_buffer_starts_empty);
    RUN_TEST(test_buffer_capacity_is_n_minus_1);
    
    // Push/pop tests
    RUN_TEST(test_push_single_sample);
    RUN_TEST(test_pop_single_sample);
    RUN_TEST(test_pop_from_empty_returns_false);
    RUN_TEST(test_push_pop_fifo_order);
    RUN_TEST(test_push_pop_interleaved);
    
    // Full/overflow tests
    RUN_TEST(test_buffer_detects_full);
    RUN_TEST(test_push_to_full_buffer_fails);
    RUN_TEST(test_overflow_counter_increments);
    RUN_TEST(test_clear_overflow);
    
    // Batch operation tests
    RUN_TEST(test_pop_batch_all_available);
    RUN_TEST(test_pop_batch_partial);
    RUN_TEST(test_pop_batch_empty_returns_zero);
    
    // Statistics tests
    RUN_TEST(test_total_pushed_counter);
    RUN_TEST(test_reset_stats);
    
    // Reset tests
    RUN_TEST(test_reset_clears_buffer);
    
    // Wraparound tests
    RUN_TEST(test_wraparound_behavior);
    
    // Edge case tests
    RUN_TEST(test_negative_adc_values);
    RUN_TEST(test_max_adc_values);
    
    return UNITY_END();
}




