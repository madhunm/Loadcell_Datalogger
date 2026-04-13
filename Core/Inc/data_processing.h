/**
 * @file    data_processing.h
 * @brief   Two-stage boxcar decimation, force calculation, and record assembly.
 * @details dpFeedSample() is called from the DMA-complete ISR at 64 kHz.
 *          Stage 1 (8-sample boxcar) emits binAdcRecord_t at 8 kHz.
 *          Stage 2 (16 stage-1 sums = 128 raw) emits binForceRecord_t at 500 Hz
 *          including a blocking SPI2 IMU read and ratiometric force calc.
 *          Records are placed in staging globals with pending flags; the main
 *          loop drains them (Phase 11 pushes to ring buffer).
 *
 *          Upstream:  adsFastComplete() in adc_ads131m02.c
 *          Downstream: main loop polling, debug_ui force display, Phase 11 ring
 *
 * @author  Madhu
 * @date    2026-04-12
 */

#ifndef DATA_PROCESSING_H
#define DATA_PROCESSING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "log_record.h"
#include "calibration.h"

/* ── Initialisation ───────────────────────────────────────────────── */

/**
 * @brief  Initialise the decimation pipeline.
 * @param[in] cal  Pointer to the active calibration config (must remain valid).
 * @pre    calibrationLoad() must have been called.
 * @post   All accumulators zeroed, sequence counters reset, logStartTick captured.
 */
void dpInit(const calConfig_t *cal);

/* ── ISR entry point (called at 64 kHz from DMA-complete) ─────────── */

/**
 * @brief  Feed one raw ADC sample into the decimation pipeline.
 * @param[in] ch0        Sign-extended 24-bit CH0 value.
 * @param[in] ch1        Sign-extended 24-bit CH1 value.
 * @param[in] adsStatus  16-bit ADS131M02 STATUS word from the SPI frame.
 * @note   Called from ISR context; must complete within a few microseconds
 *         on the 64 kHz path.  The 500 Hz path includes a blocking SPI2 IMU
 *         read (~28 us) which is safe because EXTI2 can preempt it.
 */
void dpFeedSample(int32_t ch0, int32_t ch1, uint16_t adsStatus);

/* ── Staging areas (written by ISR, read by main loop) ────────────── */

extern volatile uint8_t      g_dpPendingAdcRecord;
extern volatile uint8_t      g_dpPendingForceRecord;

extern binAdcRecord_t        g_dpStagedAdc;
extern binForceRecord_t      g_dpStagedForce;

/* ── Deferred IMU fill (called from main loop, NOT ISR) ───────────── */

/**
 * @brief  Read raw IMU data via SPI2 and fill the staged force record.
 * @param[in,out] rec  Pointer to the force record to populate with IMU data.
 * @note   Must be called from thread (main loop) context only — uses blocking
 *         SPI2 which deadlocks if called from a priority-0 ISR (SysTick masked).
 * @pre    g_dpPendingForceRecord was 1; caller has cleared the flag.
 */
void dpFillImu(binForceRecord_t *rec);

/* ── Tare ─────────────────────────────────────────────────────────── */

/**
 * @brief  Set the tare offset to the current latest force reading.
 * @note   Reads the most recent 500 Hz forceN and stores it as the cal offset.
 *         For best accuracy, call while the loadcell is unloaded and steady.
 */
void dpTare(void);

/**
 * @brief  Set an explicit tare offset value.
 * @param[in] offsetN  Tare offset in Newtons.
 */
void dpSetTareOffset(float offsetN);

/* ── Read latest force (main loop, UI) ────────────────────────────── */

/**
 * @brief  Get the most recent 500 Hz force value (N).
 * @return Latest forceN, or 0.0f if no force record has been emitted yet.
 */
float dpGetLatestForceN(void);

/**
 * @brief  Get the cumulative count of ADC records emitted (8 kHz).
 * @return Total binAdcRecord_t records since dpInit().
 */
uint32_t dpGetAdcRecordCount(void);

/**
 * @brief  Get the cumulative count of force records emitted (500 Hz).
 * @return Total binForceRecord_t records since dpInit().
 */
uint32_t dpGetForceRecordCount(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_PROCESSING_H */
