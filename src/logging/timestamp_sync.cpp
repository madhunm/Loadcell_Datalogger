/**
 * @file timestamp_sync.cpp
 * @brief RTC 1Hz Timestamp Discipline Implementation
 */

#include "timestamp_sync.h"
#include "../pin_config.h"
#include "../drivers/rx8900ce.h"
#include <esp_log.h>
#include <atomic>

static const char* TAG = "TimestampSync";

namespace TimestampSync {

namespace {
    // State
    bool initialized = false;
    std::atomic<bool> synchronized{false};
    std::atomic<uint32_t> pulseCount{0};
    
    // Time anchors (double-buffered for thread safety)
    volatile uint64_t anchorEpochSec = 0;
    volatile uint64_t anchorLocalMicros = 0;
    volatile uint64_t prevAnchorLocalMicros = 0;
    
    // Drift measurement
    volatile int32_t measuredDriftPPM = 0;
    volatile uint32_t lastPulseMicros = 0;
    
    // Expected microseconds per second (nominally 1,000,000)
    // Adjusted based on measured drift
    volatile int64_t microsPerSecond = 1000000;
    
    // Drift averaging (simple exponential moving average)
    constexpr int32_t DRIFT_ALPHA_SHIFT = 3;  // Alpha = 1/8
    volatile int64_t driftAccumulator = 0;
    volatile bool driftValid = false;
    
    /**
     * @brief ISR handler for RTC 1Hz pulse (IRAM)
     */
    void IRAM_ATTR rtcPulseISR() {
        uint64_t nowMicros = esp_timer_get_time();
        
        // Calculate elapsed since last pulse
        if (anchorLocalMicros > 0) {
            uint64_t elapsed = nowMicros - anchorLocalMicros;
            
            // Compute drift: how many microseconds off from 1,000,000
            int64_t drift = (int64_t)elapsed - 1000000LL;
            
            // Exponential moving average
            if (driftValid) {
                driftAccumulator = driftAccumulator - (driftAccumulator >> DRIFT_ALPHA_SHIFT) + drift;
            } else {
                driftAccumulator = drift << DRIFT_ALPHA_SHIFT;
                driftValid = true;
            }
            
            // Convert to PPM: drift_us / 1,000,000 * 1,000,000 = drift_us
            measuredDriftPPM = (int32_t)(driftAccumulator >> DRIFT_ALPHA_SHIFT);
            
            // Adjust microseconds per second for interpolation
            microsPerSecond = 1000000LL + measuredDriftPPM;
        }
        
        // Update anchors
        prevAnchorLocalMicros = anchorLocalMicros;
        anchorLocalMicros = nowMicros;
        lastPulseMicros = (uint32_t)nowMicros;
        
        // Increment epoch second (will be corrected in update() if needed)
        if (synchronized) {
            anchorEpochSec = anchorEpochSec + 1;
        }
        
        pulseCount++;
        synchronized = true;
    }
}

// ============================================================================
// Public API
// ============================================================================

bool init() {
    if (initialized) return true;
    
    // Configure RTC FOUT pin as input with pull-down
    // (FOUT is open-drain, needs external pull-up on hardware)
    pinMode(PIN_RTC_FOUT, INPUT);
    
    // Check initial pin state
    int pinState = digitalRead(PIN_RTC_FOUT);
    ESP_LOGI(TAG, "FOUT pin (GPIO%d) initial state: %d", PIN_RTC_FOUT, pinState);
    
    // Check if interrupt can be attached
    int intNum = digitalPinToInterrupt(PIN_RTC_FOUT);
    if (intNum < 0) {
        ESP_LOGE(TAG, "GPIO%d does not support interrupts!", PIN_RTC_FOUT);
        return false;
    }
    
    // Attach interrupt on rising edge
    attachInterrupt(intNum, rtcPulseISR, RISING);
    ESP_LOGI(TAG, "Interrupt attached on GPIO%d (int %d)", PIN_RTC_FOUT, intNum);
    
    // Initialize epoch from RTC
    time_t rtcTime = RX8900CE::getEpoch();
    if (rtcTime > 0) {
        anchorEpochSec = rtcTime;
        anchorLocalMicros = esp_timer_get_time();
        ESP_LOGI(TAG, "Initial epoch: %llu (%lu)", anchorEpochSec, (uint32_t)rtcTime);
    } else {
        ESP_LOGW(TAG, "Could not read RTC time");
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Initialized, waiting for RTC 1Hz sync on GPIO%d", PIN_RTC_FOUT);
    return true;
}

bool isInitialized() {
    return initialized;
}

bool isSynchronized() {
    return synchronized;
}

uint64_t getMicros() {
    if (!initialized) return esp_timer_get_time();
    
    uint64_t nowMicros = esp_timer_get_time();
    
    if (!synchronized || anchorLocalMicros == 0) {
        return nowMicros;
    }
    
    // Calculate elapsed since anchor
    uint64_t elapsed = nowMicros - anchorLocalMicros;
    
    // Apply drift correction
    // If ESP32 runs fast (positive drift), we need to reduce the elapsed time
    // corrected = elapsed * 1,000,000 / (1,000,000 + drift_ppm)
    if (microsPerSecond != 1000000 && microsPerSecond > 0) {
        elapsed = (elapsed * 1000000LL) / microsPerSecond;
    }
    
    return anchorLocalMicros + elapsed;
}

uint64_t getEpochMicros() {
    if (!initialized || !synchronized) {
        // Fallback to RTC
        return (uint64_t)RX8900CE::getEpoch() * 1000000ULL;
    }
    
    uint64_t nowMicros = esp_timer_get_time();
    uint64_t elapsed = nowMicros - anchorLocalMicros;
    
    // Apply drift correction
    if (microsPerSecond != 1000000 && microsPerSecond > 0) {
        elapsed = (elapsed * 1000000LL) / microsPerSecond;
    }
    
    // Combine epoch seconds with interpolated microseconds
    uint64_t epochMicros = anchorEpochSec * 1000000ULL + elapsed;
    
    return epochMicros;
}

uint32_t getEpochSeconds() {
    if (!initialized || !synchronized) {
        return RX8900CE::getEpoch();
    }
    return (uint32_t)anchorEpochSec;
}

uint32_t getMicrosInSecond() {
    if (!initialized || !synchronized) {
        return (uint32_t)(esp_timer_get_time() % 1000000);
    }
    
    uint64_t nowMicros = esp_timer_get_time();
    uint64_t elapsed = nowMicros - anchorLocalMicros;
    
    // Apply drift correction
    if (microsPerSecond != 1000000 && microsPerSecond > 0) {
        elapsed = (elapsed * 1000000LL) / microsPerSecond;
    }
    
    return (uint32_t)(elapsed % 1000000);
}

int32_t getDriftPPM() {
    return measuredDriftPPM;
}

float getDriftPPMFloat() {
    // Return the actual accumulated drift with more precision
    if (!driftValid) return 0.0f;
    return (float)(driftAccumulator >> DRIFT_ALPHA_SHIFT);
}

float getRTCTemperature() {
    return RX8900CE::getTemperature();
}

uint32_t getPulseCount() {
    return pulseCount.load();
}

SyncStatus getStatus() {
    SyncStatus status;
    status.initialized = initialized;
    status.synchronized = synchronized;
    status.pulseCount = pulseCount;
    status.driftPPM = measuredDriftPPM;
    
    if (lastPulseMicros > 0) {
        status.lastPulseAgeMs = (esp_timer_get_time() - lastPulseMicros) / 1000;
    } else {
        status.lastPulseAgeMs = UINT32_MAX;
    }
    
    return status;
}

TimeAnchor getLastAnchor() {
    TimeAnchor anchor;
    anchor.epochSeconds = anchorEpochSec;
    anchor.localMicros = anchorLocalMicros;
    return anchor;
}

void resync() {
    ESP_LOGI(TAG, "Resynchronizing...");
    
    // Reset drift measurement
    driftAccumulator = 0;
    driftValid = false;
    measuredDriftPPM = 0;
    microsPerSecond = 1000000;
    
    // Reset synchronization state
    synchronized = false;
    anchorLocalMicros = 0;
    prevAnchorLocalMicros = 0;
    
    // Re-read RTC time
    time_t rtcTime = RX8900CE::getEpoch();
    if (rtcTime > 0) {
        anchorEpochSec = rtcTime;
    }
}

void update() {
    if (!initialized) return;
    
    static uint32_t lastDebugMs = 0;
    static uint32_t lastPulseCountReported = 0;
    uint32_t now = millis();
    
    // Debug output every 5 seconds
    if (now - lastDebugMs > 5000) {
        lastDebugMs = now;
        
        uint32_t currentPulseCount = pulseCount.load();
        uint32_t pulsesInPeriod = currentPulseCount - lastPulseCountReported;
        lastPulseCountReported = currentPulseCount;
        
        // Get RTC temperature for drift correlation
        float rtcTemp = RX8900CE::getTemperature();
        
        // Check FOUT pin state
        int pinState = digitalRead(PIN_RTC_FOUT);
        
        ESP_LOGI(TAG, "Sync: pulses=%lu (+%lu), drift=%ld ppm, temp=%.1f°C, FOUT=%d",
                 currentPulseCount, pulsesInPeriod, measuredDriftPPM, rtcTemp, pinState);
        
        if (currentPulseCount == 0) {
            ESP_LOGW(TAG, "No pulses received! Check RTC FOUT config and wiring.");
        } else if (pulsesInPeriod < 4 || pulsesInPeriod > 6) {
            // Allow some tolerance (4-6 pulses in 5s is acceptable)
            ESP_LOGW(TAG, "Pulse rate anomaly: expected ~5 in 5s, got %lu", pulsesInPeriod);
        }
    }
    
    // Check for missed pulses
    if (lastPulseMicros > 0) {
        uint32_t age = (esp_timer_get_time() - lastPulseMicros) / 1000;
        
        if (synchronized && age > 2000) {
            ESP_LOGW(TAG, "No RTC pulse for %lu ms", age);
        }
    }
    
    // Periodically verify epoch against RTC (every ~60 seconds)
    static uint32_t lastRtcCheck = 0;
    
    if (synchronized && (now - lastRtcCheck) > 60000) {
        lastRtcCheck = now;
        
        time_t rtcTime = RX8900CE::getEpoch();
        int64_t diff = (int64_t)rtcTime - (int64_t)anchorEpochSec;
        
        if (diff < -1 || diff > 1) {
            ESP_LOGW(TAG, "Epoch drift detected (%lld s), resyncing", diff);
            anchorEpochSec = rtcTime;
        }
    }
}

} // namespace TimestampSync

