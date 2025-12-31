/**
 * @file timestamp_sync.cpp
 * @brief Implementation of RTC-disciplined timestamp synchronization
 */

#include "timestamp_sync.h"

// Static instance pointer for ISR
TimestampSync* TimestampSync::instance = nullptr;

void IRAM_ATTR TimestampSync::syncPulseISR() {
    if (instance) {
        instance->updateAnchor();
    }
}

bool TimestampSync::begin(uint32_t rtc_unix_time) {
    instance = this;
    logging_active = false;
    last_rtc_unix = rtc_unix_time;
    
    // Set initial anchor points
    rtc_anchor_us = (uint64_t)rtc_unix_time * 1000000ULL;
    esp_anchor_us = micros();
    
    // Configure 1Hz sync input pin
    pinMode(PIN_RTC_FOUT, INPUT);
    
    // Attach interrupt to rising edge of 1Hz pulse
    attachInterrupt(digitalPinToInterrupt(PIN_RTC_FOUT), syncPulseISR, RISING);
    
    Serial.println("Timestamp sync initialized");
    Serial.printf("RTC anchor: %llu us\n", rtc_anchor_us);
    
    return true;
}

void IRAM_ATTR TimestampSync::updateAnchor() {
    // Called from ISR on each 1Hz pulse
    // Update ESP32 anchor but increment RTC anchor by exactly 1 second
    esp_anchor_us = micros();
    rtc_anchor_us += 1000000ULL;  // Add exactly 1 second
}

uint64_t TimestampSync::getMicroseconds() {
    // Get current ESP32 microsecond counter
    uint64_t esp_now = micros();
    
    // Temporarily disable interrupts to read volatile 64-bit values atomically
    noInterrupts();
    uint64_t anchor_rtc = rtc_anchor_us;
    uint64_t anchor_esp = esp_anchor_us;
    interrupts();
    
    // Calculate elapsed time since last anchor
    uint64_t esp_elapsed = esp_now - anchor_esp;
    
    // Return RTC anchor time + elapsed time
    return anchor_rtc + esp_elapsed;
}

uint32_t TimestampSync::getRelativeMicroseconds() {
    if (!logging_active) {
        return 0;
    }
    
    uint64_t now = getMicroseconds();
    
    // Handle potential overflow (unlikely in practice)
    if (now < logging_start_us) {
        return 0;
    }
    
    // Calculate offset and clamp to uint32_t range
    uint64_t offset = now - logging_start_us;
    if (offset > 0xFFFFFFFFULL) {
        return 0xFFFFFFFF;  // Max value if overflow
    }
    
    return (uint32_t)offset;
}

void TimestampSync::startLogging() {
    logging_start_us = getMicroseconds();
    logging_active = true;
    
    Serial.printf("Logging started at: %llu us\n", logging_start_us);
}

void TimestampSync::updateRTCAnchor(uint32_t new_rtc_unix_time) {
    noInterrupts();
    last_rtc_unix = new_rtc_unix_time;
    rtc_anchor_us = (uint64_t)new_rtc_unix_time * 1000000ULL;
    esp_anchor_us = micros();
    interrupts();
    
    Serial.printf("RTC anchor updated: %llu us\n", rtc_anchor_us);
}

uint32_t TimestampSync::getTimeSinceSync() {
    uint64_t now = getMicroseconds();
    
    noInterrupts();
    uint64_t anchor = rtc_anchor_us;
    interrupts();
    
    if (now < anchor) {
        return 0;
    }
    
    return (uint32_t)((now - anchor) / 1000000ULL);
}
