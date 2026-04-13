/**
 * @file    led_status.h
 * @brief   High-level LED status engine — public API.
 * @details Maps system health and subsystem states to LED 0/1 colours and blink
 *          patterns per IEC 60073 industrial conventions.
 *          LED 0 = system (power, battery, charging, error).
 *          LED 1 = subsystems + logging (ADC, IMU, SD, Logger with rotation).
 *          Upstream: battery_monitor, imu_lsm6dsv, adc_ads131m02, app_state.
 *          Downstream: neopixel.c (low-level driver).
 * @author  Madhu
 * @date    2026-04-12
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ── Timing constants (IEC 60073) ─────────────────────────────── */
#define LED_SLOW_BLINK_MS       500u    /**< 1 Hz warning cadence (on half)          */
#define LED_FAST_BLINK_MS       125u    /**< 4 Hz error cadence (on half)            */
#define LED_HEARTBEAT_ON_MS     100u    /**< Heartbeat flash duration                */
#define LED_HEARTBEAT_PERIOD_MS 2000u   /**< Heartbeat full cycle                    */
#define LED_ROTATE_HOLD_MS      2000u   /**< Subsystem rotation: time per slot       */
#define LED_ROTATE_GAP_MS       200u    /**< OFF gap between rotation slots          */
#define LED_UPDATE_INTERVAL_MS  50u     /**< ledStatusUpdate() call rate (~20 Hz)    */

/* ── Blink patterns ───────────────────────────────────────────── */

/** @brief  Blink pattern modes — IEC 60073 industrial convention. */
typedef enum {
    LED_PATTERN_OFF,            /**< Always off                              */
    LED_PATTERN_SOLID,          /**< Always on — normal operation            */
    LED_PATTERN_SLOW_BLINK,     /**< 500 ms on / 500 ms off — warning       */
    LED_PATTERN_FAST_BLINK,     /**< 125 ms on / 125 ms off — error         */
    LED_PATTERN_HEARTBEAT,      /**< 100 ms flash, 1900 ms off — alive      */
} ledPattern_t;

/* ── LED 0: System states ─────────────────────────────────────── */

/** @brief  LED 0 system states — set directly, higher enum = higher priority. */
typedef enum {
    LED_SYS_BOOT,               /**< RED heartbeat — initialising           */
    LED_SYS_IDLE,               /**< RED solid — power ON (customer spec)   */
    LED_SYS_CHARGING,           /**< BLUE slow blink — BQ24012 active       */
    LED_SYS_BATT_LOW,           /**< ORANGE solid (customer spec)           */
    LED_SYS_BATT_CRIT,          /**< ORANGE fast blink — urgent             */
    LED_SYS_ERROR,              /**< RED fast blink — fatal                 */
} ledSysState_t;

/* ── LED 1: Subsystem identifiers and health ──────────────────── */

/** @brief  LED 1 subsystem identifiers — rotation order. */
typedef enum {
    LED_SUB_ADC,                /**< ADS131M02 ADC                          */
    LED_SUB_IMU,                /**< LSM6DSV IMU                            */
    LED_SUB_SD,                 /**< SD card / FatFS                        */
    LED_SUB_LOGGER,             /**< Data-logging state machine             */
    LED_SUB_COUNT,              /**< Sentinel — number of subsystems        */
} ledSubSystem_t;

/** @brief  Per-subsystem health level for LED 1. */
typedef enum {
    LED_LEVEL_OK,               /**< GREEN (heartbeat if idle, solid if logging) */
    LED_LEVEL_WARN,             /**< BLUE slow blink                        */
    LED_LEVEL_ERROR,            /**< RED fast blink                         */
} ledSubLevel_t;

/* ── Public functions ─────────────────────────────────────────── */

/**
 * @brief  Initialise the LED status engine.  Sets LED 0 to RED HEARTBEAT (boot).
 * @pre    neoInit() must have succeeded.
 * @post   LED 0 = RED HEARTBEAT, LED 1 = OFF.
 */
void ledStatusInit(void);

/**
 * @brief  Evaluate states and update both LEDs.  Call from main loop at ~20 Hz.
 * @details Evaluates LED 0 system state, runs LED 1 rotation engine,
 *          applies blink pattern timing, and calls neoShow().
 * @note   All timing derived from HAL_GetTick(); no ISR involvement.
 */
void ledStatusUpdate(void);

/**
 * @brief  Set the system-level state for LED 0.
 * @param[in] state  New system state (applied in next ledStatusUpdate).
 */
void ledStatusSetSys(ledSysState_t state);

/**
 * @brief  Set per-subsystem health level for LED 1.
 * @param[in] sys    Which subsystem (ADC, IMU, SD, Logger).
 * @param[in] level  Health level (OK, WARN, ERROR).
 */
void ledStatusSetSub(ledSubSystem_t sys, ledSubLevel_t level);

/**
 * @brief  Set the logging-active flag for LED 1.
 * @param[in] active  true = logging (GREEN SOLID), false = idle.
 * @note   When logging + warnings coexist, rotation includes a GREEN SOLID slot.
 */
void ledStatusSetLogging(bool active);

/**
 * @brief  Get a short diagnostic string describing the current LED state.
 * @return Static string, e.g. "sys=IDLE sub=OK/OK/OK/OK log=0".
 * @note   For UART debug prints only; not reentrant.
 */
const char *ledStatusGetDiagStr(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_STATUS_H */
