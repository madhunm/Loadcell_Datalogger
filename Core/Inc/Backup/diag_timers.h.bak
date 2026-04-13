/**
 * @file diag_timers.h
 * @brief Diagnostic timer services — CLKIN frequency measurement and DRDY edge counting.
 * @details TIM8 in external-clock mode counts LTC6903 CLKIN edges on PC6 (TI1FP1).
 *          TIM3 in external-clock mode counts ADS131M02 DRDY edges on PB4 (TI1FP1).
 *          DWT CYCCNT provides nanosecond-precision SYSCLK-referenced timing for
 *          the high-accuracy CLKIN measurement used by the LTC6903 auto-trim.
 *
 *          Always compiled; used for production diagnostics (metadata records).
 *
 * @author Madhu
 * @date   2026-04-12
 * @see    RM0481 §33 (TIM8 general-purpose timer), §35.4.3 (external clock mode 1)
 */

#ifndef DIAG_TIMERS_H
#define DIAG_TIMERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── CLKIN measurement (TIM8 / PC6) ────────────────────────────── */

/**
 * @brief  Initialise TIM8 as an external-clock counter on PC6 (LTC6903 CLKIN).
 * @details Overrides the CubeMX slave-mode trigger from TI2FP2 to TI1FP1 so
 *          the counter receives edges on the correct pin.  Enables the TIM8 update
 *          (overflow) interrupt to track 16-bit counter wraps at ~125 Hz.
 * @pre    MX_TIM8_Init() called.
 * @post   TIM8 running, overflow interrupt at priority 10.
 */
void     diagClkinInit(void);

/**
 * @brief  Periodic 1 Hz poll — compute CLKIN frequency from TIM8 edge delta.
 * @details Called from the main loop.  Updates the internal cache read by
 *          diagClkinGetHz() and pushes the value to the VT220 UI.
 */
void     diagClkinPoll(void);

/**
 * @brief  Return the most recently computed CLKIN frequency in Hz (1 Hz update).
 * @return Frequency in Hz, or 0 if no measurement available yet.
 */
uint32_t diagClkinGetHz(void);

/**
 * @brief  Enable the DWT cycle counter (CYCCNT) if not already running.
 * @note   Safe to call multiple times; idempotent.
 */
void     diagClkinEnsureDwt(void);

/**
 * @brief  Blocking high-accuracy CLKIN measurement using DWT CYCCNT.
 * @param[in] durationMs  Measurement window in milliseconds.
 * @return Measured CLKIN frequency in Hz.
 * @pre    diagClkinInit() called.  DWT enabled (called internally if needed).
 * @note   Blocks for the requested duration.  Do not call while USB CDC needs
 *         servicing unless duration is short (< 100 ms).
 */
uint32_t diagClkinMeasureHz(uint32_t durationMs);

/**
 * @brief  Run a multi-second stability test printing per-second CLKIN and SYSCLK stats.
 * @param[in] durationS  Test duration in seconds.
 * @note   Calls ux_system_tasks_run() and cdcPoll() during the measurement to
 *         keep USB alive.
 */
void     diagClkinStabilityTest(uint32_t durationS);

/* ── DRDY hardware edge counter (TIM3 / PB4) ──────────────────── */

/**
 * @brief  Initialise TIM3 as an external-clock counter on PB4 (ADS131M02 DRDY).
 * @details Reconfigures from input-capture to external-clock mode 1, falling edge.
 *          Prescaler set to 0 so every DRDY edge increments the counter.
 * @pre    MX_TIM3_Init() called.
 * @post   TIM3 running, overflow interrupt at priority 8.
 */
void     diagDrdyInit(void);

/**
 * @brief  Atomically read the total DRDY edge count (overflow-extended to 32 bits).
 * @return Cumulative edge count since diagDrdyInit().
 */
uint32_t diagDrdyReadEdges(void);

/**
 * @brief  TIM3 overflow handler — called from HAL_TIM_PeriodElapsedCallback().
 */
void     diagDrdyTim3Overflow(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_TIMERS_H */
