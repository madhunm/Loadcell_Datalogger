/**
 * @file timestamp_sync.h
 * @brief RTC-disciplined timestamp generation using 1Hz sync signal
 */

#ifndef TIMESTAMP_SYNC_H
#define TIMESTAMP_SYNC_H

#include <Arduino.h>
#include "../pin_config.h"

/**
 * @brief Manages timestamp synchronization using RTC 1Hz output
 * 
 * The RTC provides a 1Hz square wave on FOUT pin. We capture the edge
 * and record the ESP32's microsecond counter, creating anchor points.
 * Between anchors, we interpolate using the ESP32's high-resolution timer.
 */
class TimestampSync {
public:
    /**
     * @brief Initialize timestamp synchronization
     * @param rtc_unix_time Current RTC Unix timestamp in seconds
     * @return true if successful
     */
    bool begin(uint32_t rtc_unix_time);
    
    /**
     * @brief Get current synchronized timestamp in microseconds since epoch
     * @return Timestamp in microseconds
     */
    uint64_t getMicroseconds();
    
    /**
     * @brief Get relative timestamp from logging start
     * @return Microseconds since startLogging() was called
     */
    uint32_t getRelativeMicroseconds();
    
    /**
     * @brief Mark the start of logging session
     * Captures current absolute timestamp as reference point
     */
    void startLogging();
    
    /**
     * @brief Check if logging session is active
     * @return true if startLogging() has been called
     */
    bool isLoggingActive() const { return logging_active; }
    
    /**
     * @brief Update RTC anchor point (call when RTC is updated)
     * @param new_rtc_unix_time New RTC Unix timestamp in seconds
     */
    void updateRTCAnchor(uint32_t new_rtc_unix_time);
    
    /**
     * @brief Get time since last RTC sync
     * @return Seconds since last anchor update
     */
    uint32_t getTimeSinceSync();
    
private:
    volatile uint64_t rtc_anchor_us;          ///< RTC time in microseconds at last sync
    volatile uint64_t esp_anchor_us;          ///< ESP32 micros() at last sync
    uint64_t logging_start_us;       ///< Absolute timestamp when logging started
    bool logging_active;             ///< true if currently logging
    uint32_t last_rtc_unix;          ///< Last RTC Unix timestamp used
    
    /**
     * @brief ISR handler for 1Hz sync pulse
     */
    static void IRAM_ATTR syncPulseISR();
    
    /**
     * @brief Update anchor points from ISR
     */
    void IRAM_ATTR updateAnchor();
    
    static TimestampSync* instance;  ///< Singleton for ISR access
};

#endif // TIMESTAMP_SYNC_H
