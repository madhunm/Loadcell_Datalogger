/**
 * @file ring_buffer.h
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) Ring Buffer
 * 
 * Designed for zero-loss ADC data acquisition at 64 ksps.
 * 
 * Features:
 * - Lock-free operation (no mutexes)
 * - Single producer (ISR on Core 1)
 * - Single consumer (SD writer on Core 0)
 * - Overflow detection before push
 * - Cache-line aligned for performance
 * 
 * Memory layout:
 * - 32KB buffer = 4096 samples @ 8 bytes each
 * - Provides ~64ms of buffering at 64 ksps
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <Arduino.h>
#include <atomic>

/**
 * @brief ADC sample with timestamp
 * 
 * 8 bytes per sample for efficient memory alignment.
 */
struct ADCSample {
    int32_t raw;           ///< 24-bit ADC value (sign-extended to 32-bit)
    uint32_t timestamp_us; ///< Microsecond timestamp from esp_timer
};

/**
 * @brief Lock-free SPSC ring buffer for ADC samples
 * 
 * Thread-safe for single producer (ISR) and single consumer (task).
 * Uses atomic operations and memory barriers for correct ordering.
 * 
 * @tparam CAPACITY Number of samples the buffer can hold (must be power of 2)
 */
template<size_t CAPACITY>
class RingBuffer {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be power of 2");
    
public:
    RingBuffer() : head_(0), tail_(0), overflowCount_(0), totalPushed_(0) {
        // Zero-initialize the buffer
        memset(buffer_, 0, sizeof(buffer_));
    }
    
    /**
     * @brief Push a sample to the buffer (producer side - ISR safe)
     * 
     * MUST call isFull() before push() to implement zero-loss policy.
     * If buffer is full, push will fail and increment overflow counter.
     * 
     * @param sample The ADC sample to push
     * @return true if push succeeded, false if buffer was full
     */
    bool IRAM_ATTR push(const ADCSample& sample) {
        size_t currentHead = head_.load(std::memory_order_relaxed);
        size_t nextHead = (currentHead + 1) & MASK;
        
        // Check if buffer is full
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            overflowCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // Store sample
        buffer_[currentHead] = sample;
        
        // Memory barrier to ensure sample is written before head advances
        std::atomic_thread_fence(std::memory_order_release);
        
        // Advance head
        head_.store(nextHead, std::memory_order_release);
        totalPushed_.fetch_add(1, std::memory_order_relaxed);
        
        return true;
    }
    
    /**
     * @brief Pop a sample from the buffer (consumer side)
     * 
     * @param sample Output parameter for the popped sample
     * @return true if a sample was available, false if buffer was empty
     */
    bool pop(ADCSample& sample) {
        size_t currentTail = tail_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (currentTail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Read sample
        sample = buffer_[currentTail];
        
        // Memory barrier to ensure sample is read before tail advances
        std::atomic_thread_fence(std::memory_order_release);
        
        // Advance tail
        size_t nextTail = (currentTail + 1) & MASK;
        tail_.store(nextTail, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief Pop multiple samples at once (consumer side, more efficient)
     * 
     * @param samples Output array for samples
     * @param maxCount Maximum number of samples to pop
     * @return Number of samples actually popped
     */
    size_t popBatch(ADCSample* samples, size_t maxCount) {
        size_t count = 0;
        size_t currentTail = tail_.load(std::memory_order_relaxed);
        size_t currentHead = head_.load(std::memory_order_acquire);
        
        while (count < maxCount && currentTail != currentHead) {
            samples[count++] = buffer_[currentTail];
            currentTail = (currentTail + 1) & MASK;
        }
        
        if (count > 0) {
            std::atomic_thread_fence(std::memory_order_release);
            tail_.store(currentTail, std::memory_order_release);
        }
        
        return count;
    }
    
    /**
     * @brief Check if buffer is full (ISR safe)
     * 
     * Call this BEFORE push() to implement zero-loss policy.
     * 
     * @return true if buffer cannot accept more samples
     */
    bool IRAM_ATTR isFull() const {
        size_t nextHead = (head_.load(std::memory_order_relaxed) + 1) & MASK;
        return nextHead == tail_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Check if buffer is empty
     * 
     * @return true if no samples available
     */
    bool isEmpty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get number of samples available to read
     * 
     * @return Number of samples in buffer
     */
    size_t available() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & MASK;
    }
    
    /**
     * @brief Get free space in buffer
     * 
     * @return Number of samples that can be pushed
     */
    size_t freeSpace() const {
        return CAPACITY - available() - 1;  // -1 because we never fill completely
    }
    
    /**
     * @brief Get buffer capacity
     * 
     * @return Maximum number of samples buffer can hold
     */
    constexpr size_t capacity() const {
        return CAPACITY - 1;  // Actual usable capacity
    }
    
    /**
     * @brief Get overflow count (samples lost due to full buffer)
     * 
     * Non-zero value indicates zero-loss policy was violated!
     * 
     * @return Number of samples that failed to push
     */
    uint32_t getOverflowCount() const {
        return overflowCount_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get total samples successfully pushed
     * 
     * @return Total push count since creation/reset
     */
    uint32_t getTotalPushed() const {
        return totalPushed_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if overflow has occurred
     * 
     * @return true if any samples were lost
     */
    bool hasOverflow() const {
        return overflowCount_.load(std::memory_order_relaxed) > 0;
    }
    
    /**
     * @brief Reset the buffer to empty state
     * 
     * WARNING: Only call when no producer/consumer is active!
     */
    void reset() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        // Don't reset overflow count - keep for diagnostics
    }
    
    /**
     * @brief Clear overflow counter
     * 
     * Call after handling/logging overflow condition.
     */
    void clearOverflow() {
        overflowCount_.store(0, std::memory_order_relaxed);
    }
    
    /**
     * @brief Reset all statistics
     */
    void resetStats() {
        overflowCount_.store(0, std::memory_order_relaxed);
        totalPushed_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr size_t MASK = CAPACITY - 1;
    
    // Cache line alignment to prevent false sharing
    alignas(64) std::atomic<size_t> head_;  // Written by producer (ISR)
    alignas(64) std::atomic<size_t> tail_;  // Written by consumer
    
    // Statistics
    std::atomic<uint32_t> overflowCount_;
    std::atomic<uint32_t> totalPushed_;
    
    // Sample storage
    ADCSample buffer_[CAPACITY];
};

// ============================================================================
// Type Aliases for Common Configurations
// ============================================================================

/**
 * @brief Standard ADC ring buffer (4096 samples = 32KB)
 * 
 * At 64 ksps, provides ~64ms of buffering.
 * This should be sufficient for SD card write latency spikes.
 */
using ADCRingBuffer = RingBuffer<4096>;

/**
 * @brief Large ADC ring buffer (8192 samples = 64KB)
 * 
 * At 64 ksps, provides ~128ms of buffering.
 * Use if experiencing overflow with standard buffer.
 */
using ADCRingBufferLarge = RingBuffer<8192>;

#endif // RING_BUFFER_H

