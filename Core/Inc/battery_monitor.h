/**
 * @file    battery_monitor.h
 * @brief   Battery voltage, SOC, charger decode, USB sense, and MCU temperature — public API.
 * @details Declares types and functions for once-per-second battery/system polling.
 *          Upstream: HAL ADC1 (PA1 + VSENSE), BQ24012 GPIOs, PB1 USB sense.
 *          Downstream: debug_ui (VT220 panel), led_status (LED 0 battery states).
 * @author  Madhu
 * @date    2026-04-12
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ── Charger state enum ───────────────────────────────────────── */

/** @brief  BQ24012 charger state decoded from PG/STAT1/STAT2 GPIOs. */
typedef enum {
    CHG_BATTERY,    /**< No input power — running on battery      */
    CHG_CHARGING,   /**< Input power present, charging in progress */
    CHG_FULL,       /**< Input power present, charge complete      */
    CHG_STANDBY,    /**< Input power present, standby or fault     */
} chargeState_t;

/* ── Public functions ─────────────────────────────────────────── */

/**
 * @brief  Initialise ADC1 calibration and perform an initial battery poll.
 * @return 0 on success, -1 on ADC calibration failure.
 * @pre    MX_ADC1_Init() must have been called.
 * @post   All cached readings valid; LED 0 state updated.
 */
int batteryInit(void);

/**
 * @brief  Poll battery voltage, MCU temperature, charger GPIOs, and USB sense.
 * @details Performs two sequential ADC1 conversions (PA1 battery, then VSENSE
 *          temperature), reads BQ24012 GPIOs, computes SOC from OCV table,
 *          and calls ledStatusSetSys() to update LED 0.
 * @note   Call once per second from the main loop.
 */
void batteryPoll(void);

/**
 * @brief  Get the last-measured battery voltage.
 * @return Battery voltage in volts (typically 3.0–4.2 V for 1S Li-ion).
 */
float batteryGetVoltage(void);

/**
 * @brief  Get the estimated state-of-charge.
 * @return SOC percentage 0–100, or 0xFF while charging (SOC indeterminate).
 */
uint8_t batteryGetSocPercent(void);

/**
 * @brief  Get the decoded BQ24012 charger state.
 * @return One of CHG_BATTERY, CHG_CHARGING, CHG_FULL, CHG_STANDBY.
 */
chargeState_t batteryGetChargeState(void);

/**
 * @brief  Get a human-readable string for the current charger state.
 * @return Static string: "BATTERY", "CHARGING", "FULL", or "STANDBY".
 */
const char *batteryGetChargeStateStr(void);

/**
 * @brief  Check whether USB VBUS is present.
 * @return true if PB1 (USB_SENSE) reads HIGH.
 */
bool batteryIsUsbConnected(void);

/**
 * @brief  Get the last-measured MCU die temperature in tenths of a degree.
 * @return Temperature x10 (e.g. 253 = 25.3 C).  Uses datasheet typical values
 *         (factory calibration OTP inaccessible due to flash security).
 * @see    STM32H562 datasheet DS14001 Table 100.
 */
int16_t battGetMcuTempX10(void);

/**
 * @brief  Get the last-measured VDDA voltage (from VREFINT).
 * @return Supply voltage in millivolts (nominal ~3300 mV).
 */
uint32_t battGetVddaMv(void);

/**
 * @brief  Get the last-measured VDDCORE voltage (internal regulator output).
 * @return Core voltage in millivolts (expected ~1250 mV at VOS0 / 250 MHz).
 */
uint32_t battGetVddCoreMv(void);

/**
 * @brief  Get SOC as a display string.
 * @return e.g. " 42%" or "---" when SOC is unavailable (charging).
 */
const char *batteryGetSocStr(void);

/**
 * @brief  Get a debug string showing raw charger GPIO pin states.
 * @return Static string e.g. "PG=1 S1=1 S2=1" (1=HIGH, 0=LOW).
 */
const char *batteryGetGpioDebugStr(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_MONITOR_H */
