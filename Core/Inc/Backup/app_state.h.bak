/**
 * @file    app_state.h
 * @brief   Application state machine — minimal stub for USB logging gate.
 * @details Phase 11 builds the full state machine.  This stub provides the
 *          state enum and the USB logging-gate check consumed by the button
 *          handler.
 *          Upstream: battery_monitor (USB sense).
 *          Downstream: main (button handler), logger (Phase 11).
 * @author  Madhu
 * @date    2026-04-12
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** @brief  Top-level application states (expanded in Phase 11). */
typedef enum {
    STATE_IDLE,     /**< No logging, waiting for button press */
    STATE_LOGGING,  /**< Actively writing data to SD          */
    STATE_ERROR,    /**< Fatal error, logging stopped          */
} appState_t;

/**
 * @brief  Get the current application state.
 * @return Current state (defaults to STATE_IDLE in this stub).
 */
appState_t appStateGet(void);

/**
 * @brief  Check whether logging is permitted based on USB connection.
 * @details If allowLogOnUsb == 0 and USB VBUS is present, logging is
 *          blocked to prevent data corruption from USB enumeration resets.
 * @return true if logging may start, false if blocked by USB policy.
 * @note   allowLogOnUsb defaults to 1 (dev mode); Phase 10 loads from config.txt.
 */
bool appStateCanStartLogging(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
