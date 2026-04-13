/**
 * @file debug_ui.h
 * @brief VT220 ANSI status panel, scrolling debug log, and field-update API.
 * @details Draws a 24-row status panel over USB CDC using VT220/ANSI escape
 *          sequences (cursor positioning, scroll region, box-drawing characters).
 *          Field setters cache values and mark dirty flags; uiUpdateFields()
 *          redraws only changed rows at rate-limited intervals (100 ms fast,
 *          1 s slow).  A parallel 1 Hz UART dump outputs the same data as
 *          flat key=value lines on USART1 for headless debugging.
 *
 *          Upstream: peripheral drivers push values via uiSet*().
 *          Downstream: CDC for VT220 panel; USART1 for plain-text dump.
 *
 * @author Madhu
 * @date   2026-04-12
 */

#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Panel lifecycle ─────────────────────────────────────────────── */

/** @brief Clear the terminal and draw the full 24-row status panel (CDC only). */
void uiDrawPanel(void);

/**
 * @brief  Refresh dirty fields on the VT220 panel (CDC) and emit a 1 Hz UART dump.
 * @details Rate-limited: fast fields at 10 Hz, slow fields at 1 Hz.
 *          Automatically redraws the full panel if CDC reconnects.
 */
void uiUpdateFields(void);

/**
 * @brief  Print a formatted message to the scrolling log region below the panel.
 * @param[in] fmt  printf-style format string.
 */
void uiLog(const char *fmt, ...);

/** @brief Emit a 1 Hz plain-text status dump on USART1 (no escape sequences). */
void uiUartDump(void);

/** @brief Poll CDC RX for single-character commands (e.g. 'd' = redraw panel). */
void uiProcessInput(void);

/* ── State ───────────────────────────────────────────────────────── */

/**
 * @brief  Set the application state string displayed in the panel title row.
 * @param[in] state  Short label, e.g. "IDLE", "RUN", "LOGGING".
 */
void uiSetState(const char *state);

/* ── ADC fields ──────────────────────────────────────────────────── */

/** @brief Set the CLKIN frequency field (Hz). */
void uiSetClkinHz(uint32_t hz);

/** @brief Set the DRDY rate field (Hz, computed per-second delta). */
void uiSetDrdyHz(uint32_t hz);

/** @brief Set the raw ADC channel counts (sign-extended 24-bit). */
void uiSetAdcCounts(int32_t ch0, int32_t ch1);

/** @brief Set the voltage-ratio field (V). */
void uiSetVratio(float volts);

/** @brief Set the computed force field (N). */
void uiSetForce(float newtons);

/** @brief Set the ADC ring-buffer statistics (samples in / ok / lost). */
void uiSetAdcRing(uint32_t in, uint32_t ok, uint32_t lost);

/* ── IMU fields ──────────────────────────────────────────────────── */

/** @brief Set accelerometer X/Y/Z values (m/s²). */
void uiSetAccel(float x, float y, float z);

/** @brief Set gyroscope X/Y/Z values (dps). */
void uiSetGyro(float x, float y, float z);

/** @brief Set cumulative gyro drift X/Y/Z (degrees). */
void uiSetImuDrift(float dx, float dy, float dz);

/** @brief Set the dominant gravity-axis tag (e.g. "+Z", "-X"). */
void uiSetImuGrav(const char *tag);

/** @brief Set calibration offsets: accel (m/s²) and gyro (dps). */
void uiSetImuCal(float ax, float ay, float az,
                 float gx, float gy, float gz);

/** @brief Set the SFLP quaternion (w, x, y, z). */
void uiSetQuat(float w, float x, float y, float z);

/** @brief Set Euler angles derived from the quaternion (degrees). */
void uiSetEuler(float roll, float pitch, float yaw);

/** @brief Set the IMU die-temperature field (°C). */
void uiSetImuTemp(float degC);

/* ── System fields ───────────────────────────────────────────────── */

/**
 * @brief  Set battery voltage, state-of-charge, and charger status.
 * @param[in] volts    Battery voltage (V).
 * @param[in] socPct   State-of-charge percentage (0–100).
 * @param[in] chgStr   Charger state label ("BATTERY", "CHARGING", "FULL", "STANDBY").
 */
void uiSetBattery(float volts, uint8_t socPct, const char *chgStr);

/** @brief Set the USB connection status string ("CDC", "---"). */
void uiSetUsbStatus(const char *status);

/** @brief Set the MCU die-temperature field (°C, from VSENSE). */
void uiSetMcuTemp(float degC);

/** @brief Set the SD card status string ("READY", "ERROR", etc.). */
void uiSetSdStatus(const char *status);

/** @brief Set the calibration source string ("SD-FILE", "FLASH", "DEFAULT"). */
void uiSetCalSource(const char *source);

/** @brief Set logging-pipeline statistics (records in / ok / lost). */
void uiSetLogStats(uint32_t in, uint32_t ok, uint32_t lost);

/**
 * @brief  Set the "written" and "elapsed" fields for the active logging session.
 * @param[in] mb         Megabytes written to SD.
 * @param[in] elapsedS  Session elapsed time in seconds.
 */
void uiSetWritten(float mb, uint32_t elapsedS);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_UI_H */
