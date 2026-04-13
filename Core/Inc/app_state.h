/**
 * @file    app_state.h
 * @brief   Application state machine — logging states and USB policy gate.
 * @details States IDLE / LOGGING / STOPPING / ERROR with logStart button ISR
 *          flag consumed from the main loop (debounced).  EXTI4 for logStart only.
 * @author  Madhu
 * @date    2026-04-13
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** @brief  Top-level application states. */
typedef enum {
    STATE_IDLE,      /**< No logging */
    STATE_LOGGING,   /**< Session open, g_loggingActive may gate ISR push */
    STATE_STOPPING,  /**< Draining rings before sdSessionClose */
    STATE_ERROR,     /**< SD or fatal fault */
} appState_t;

/**
 * @brief  Get the current application state.
 * @return Current state.
 */
appState_t appStateGet(void);

/**
 * @brief  Set the application state.
 * @param[in] state  New state.
 */
void appStateSet(appState_t state);

/**
 * @brief  Check whether logging is permitted (USB policy from calibration).
 * @return true if logging may start.
 */
bool appStateCanStartLogging(void);

/**
 * @brief  EXTI callback entry — sets internal button flag (debounce in main).
 * @note   Call only from HAL_GPIO_EXTI_Rising_Callback for logStart_Pin.
 */
void appStateButtonIsr(void);

/**
 * @brief  Return true once per button interrupt edge (clears internal latch).
 * @return true if a rising edge was latched since last call.
 */
bool appStateConsumeButtonPress(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
