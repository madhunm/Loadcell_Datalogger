/**
 * @file timestamp_sync.h
 * @brief RTC 1Hz Timestamp Discipline
 * 
 * Synchronizes ESP32 microsecond counter with RTC 1Hz FOUT signal
 * for accurate, drift-corrected timestamps during data acquisition.
 * 
 * The RTC provides a stable 1Hz reference. On each rising edge,
 * we capture the ESP32 microsecond counter and compute drift.
 * Timestamps between pulses are interpolated with drift correction.
 */

#ifndef TIMESTAMP_SYNC_H
#define TIMESTAMP_SYNC_H

#include <Arduino.h>

namespace TimestampSync {

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Synchronization status */
struct SyncStatus {
    bool initialized;           // Module initialized
    bool synchronized;          // At least one RTC pulse received
    uint32_t pulseCount;        // Total 1Hz pulses received
    int32_t driftPPM;          // Measured clock drift in PPM
    uint32_t lastPulseAgeMs;   // Time since last pulse
};

/** @brief Timestamp anchor point */
struct TimeAnchor {
    uint64_t epochSeconds;      // Unix epoch seconds at anchor
    uint64_t localMicros;       // ESP32 micros() at anchor
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize timestamp synchronization
 * 
 * Sets up GPIO interrupt on RTC 1Hz FOUT pin.
 * Call after RX8900CE::init() and RX8900CE::enableFOUT1Hz().
 * 
 * @return true if initialization successful
 */
bool init();

/**
 * @brief Check if module is initialized
 */
bool isInitialized();

/**
 * @brief Check if synchronized (at least one RTC pulse received)
 */
bool isSynchronized();

/**
 * @brief Get current timestamp in microseconds (local time base)
 * 
 * Returns disciplined microsecond timestamp, correcting for
 * measured clock drift between RTC pulses.
 * 
 * @return Microseconds since boot (drift-corrected)
 */
uint64_t getMicros();

/**
 * @brief Get current timestamp as Unix epoch microseconds
 * 
 * Combines RTC time with interpolated microseconds for
 * absolute timestamp with microsecond resolution.
 * 
 * @return Unix epoch time in microseconds
 */
uint64_t getEpochMicros();

/**
 * @brief Get current timestamp as Unix epoch seconds
 * 
 * @return Unix epoch time in seconds
 */
uint32_t getEpochSeconds();

/**
 * @brief Get microseconds within current second
 * 
 * @return Microseconds (0-999999) within current RTC second
 */
uint32_t getMicrosInSecond();

/**
 * @brief Get measured clock drift in PPM (integer)
 * 
 * Positive = ESP32 clock running fast
 * Negative = ESP32 clock running slow
 * 
 * @return Drift in parts per million
 */
int32_t getDriftPPM();

/**
 * @brief Get measured clock drift in PPM (float for precision)
 * 
 * @return Drift in parts per million with decimal precision
 */
float getDriftPPMFloat();

/**
 * @brief Get RTC temperature from TCXO
 * 
 * Temperature affects crystal oscillator drift characteristics.
 * Useful for logging temperature alongside drift for analysis.
 * 
 * @return Temperature in degrees Celsius (or -128 on error)
 */
float getRTCTemperature();

/**
 * @brief Get total 1Hz pulse count
 * 
 * @return Number of RTC pulses received since init
 */
uint32_t getPulseCount();

/**
 * @brief Get synchronization status
 */
SyncStatus getStatus();

/**
 * @brief Get last time anchor
 * 
 * Returns the most recent anchor point (RTC pulse).
 * Useful for debugging and verification.
 */
TimeAnchor getLastAnchor();

/**
 * @brief Force resynchronization
 * 
 * Clears drift history and waits for new anchor.
 */
void resync();

/**
 * @brief Update function (call periodically)
 * 
 * Updates internal state, checks for missed pulses.
 * Call from main loop or a timer task.
 */
void update();

} // namespace TimestampSync

#endif // TIMESTAMP_SYNC_H

