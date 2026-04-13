/**
 * @file    data_processing.c
 * @brief   Two-stage boxcar decimation, ratiometric force, and record assembly.
 * @details Stage 1 accumulates 8 raw 64 kHz samples into a binAdcRecord_t
 *          emitted at 8 kHz.  Stage 2 accumulates 16 Stage-1 sums (= 128 raw)
 *          into a binForceRecord_t emitted at 500 Hz, which includes a blocking
 *          SPI2 IMU read and ratiometric force calculation.
 *
 *          All accumulation runs inside the DMA-complete ISR context.  The
 *          64 kHz path adds ~0.3 us; the 8 kHz path adds ~2 us (record + CRC);
 *          the 500 Hz path adds ~35 us (IMU SPI2 read + float math).  EXTI2
 *          at priority 0 preempts the continuation, so intervening DRDYs are
 *          never missed.
 *
 * @author  Madhu
 * @date    2026-04-12
 * @see     Phase 10 plan (.cursor/plans/phase_10_decimation_force_records.plan.md)
 */

#include "data_processing.h"
#include "imu_lsm6dsv.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── Module-static state ──────────────────────────────────────────── */

static const calConfig_t *pCal;

/* Stage 1 accumulators (8-sample boxcar, 64 kHz → 8 kHz) */
static int64_t  accCh0_8;
static int64_t  accCh1_8;
static uint32_t stage1Count;

/* Stage 2 accumulators (16 stage-1 sums = 128 raw, 8 kHz → 500 Hz) */
static int64_t  accCh0_128;
static int64_t  accCh1_128;
static uint32_t stage2Count;

/* Sequence counters (wrap at 16-bit) */
static uint16_t adcSeq;
static uint16_t forceSeq;

/* Timestamp base */
static uint32_t logStartTick;

/* Latest force for UI polling */
static volatile float latestForceN;

/* Cumulative record counters for rate diagnostics */
static volatile uint32_t adcRecordTotal;
static volatile uint32_t forceRecordTotal;

/* Last ADS STATUS word carried through the pipeline */
static uint16_t lastAdsStatus;

/* ── Staging areas (ISR writes, main loop reads) ──────────────────── */

volatile uint8_t      g_dpPendingAdcRecord;
volatile uint8_t      g_dpPendingForceRecord;
binAdcRecord_t        g_dpStagedAdc;
binForceRecord_t      g_dpStagedForce;

/* ── Threshold for CH1 division-by-zero guard ─────────────────────── */
#define CH1_MIN_ABS_THRESHOLD  1000

/* ── Public API ───────────────────────────────────────────────────── */

void dpInit(const calConfig_t *cal)
{
    pCal = cal;

    accCh0_8    = 0;
    accCh1_8    = 0;
    stage1Count = 0;

    accCh0_128  = 0;
    accCh1_128  = 0;
    stage2Count = 0;

    adcSeq      = 0;
    forceSeq    = 0;

    logStartTick    = HAL_GetTick();
    latestForceN    = 0.0f;
    adcRecordTotal  = 0;
    forceRecordTotal = 0;
    lastAdsStatus   = 0;

    g_dpPendingAdcRecord   = 0;
    g_dpPendingForceRecord = 0;

    memset(&g_dpStagedAdc,   0, sizeof(g_dpStagedAdc));
    memset(&g_dpStagedForce, 0, sizeof(g_dpStagedForce));
}

void dpFeedSample(int32_t ch0, int32_t ch1, uint16_t adsStatus)
{
    lastAdsStatus = adsStatus;

    /* ── Stage 1: accumulate 8 raw samples ────────────────────────── */
    accCh0_8 += ch0;
    accCh1_8 += ch1;
    stage1Count++;

    if (stage1Count < 8)
        return;

    /* ── Emit binAdcRecord_t at 8 kHz ─────────────────────────────── */
    {
        binAdcRecord_t rec;
        rec.type   = REC_TYPE_ADC;
        rec.flags  = 0;
        rec.seqLo  = adcSeq++;
        rec.sumCh0 = (int32_t)accCh0_8;
        rec.sumCh1 = (int32_t)accCh1_8;

        if (pCal->enableAdcCrc)
            rec.crc16 = crc16Ccitt((const uint8_t *)&rec, 14);
        else
            rec.crc16 = 0x0000;

        g_dpStagedAdc = rec;
        g_dpPendingAdcRecord = 1;
        adcRecordTotal++;
    }

    /* Feed Stage 2 before resetting Stage 1 accumulators */
    accCh0_128 += accCh0_8;
    accCh1_128 += accCh1_8;
    stage2Count++;

    /* Reset Stage 1 */
    accCh0_8    = 0;
    accCh1_8    = 0;
    stage1Count = 0;

    if (stage2Count < 16)
        return;

    /* ── Emit binForceRecord_t at 500 Hz ──────────────────────────── */
    {
        binForceRecord_t rec;
        rec.type        = REC_TYPE_FORCE;
        rec.validity    = VALIDITY_NO_OVERFLOW;
        rec.seqLo       = forceSeq++;
        rec.timestampMs = HAL_GetTick() - logStartTick;
        rec.sumCh0_128  = (int32_t)accCh0_128;

        /* Ratiometric force calculation with division-by-zero guard */
        int64_t absAccCh1 = accCh1_128 < 0 ? -accCh1_128 : accCh1_128;
        if (absAccCh1 >= CH1_MIN_ABS_THRESHOLD)
        {
            rec.forceN = ((float)accCh0_128 / (float)accCh1_128)
                         * (3.3f / pCal->sensitivityUvPerN) * 1e6f
                         - pCal->tareOffsetN;
            rec.validity |= VALIDITY_ADC_OK;
        }
        else
        {
            rec.forceN = 0.0f;
        }

        /* IMU raw data is filled by the main loop via dpFillImu() after
         * picking up the pending flag.  We cannot call imuReadRaw() here
         * because this runs inside the GPDMA1_CH1 ISR (prio 0) and SPI2
         * HAL blocking calls deadlock when SysTick (lower prio) is masked.
         * See BLOCKER 1 mitigation in phase_10 plan. */
        rec.accelX = 0;
        rec.accelY = 0;
        rec.accelZ = 0;
        rec.gyroX  = 0;
        rec.gyroY  = 0;
        rec.gyroZ  = 0;

        if (pCal->enableAdcCrc)
            rec.crc16 = crc16Ccitt((const uint8_t *)&rec, 30);
        else
            rec.crc16 = 0x0000;

        g_dpStagedForce = rec;
        g_dpPendingForceRecord = 1;
        forceRecordTotal++;

        latestForceN = rec.forceN;
    }

    /* Reset Stage 2 */
    accCh0_128  = 0;
    accCh1_128  = 0;
    stage2Count = 0;
}

void dpTare(void)
{
    float current = latestForceN + pCal->tareOffsetN;
    calibrationSetTare(current);
    printf("TARE: offset set to %.3f N (raw reading)\r\n", (double)current);
}

void dpSetTareOffset(float offsetN)
{
    calibrationSetTare(offsetN);
}

float dpGetLatestForceN(void)
{
    return latestForceN;
}

uint32_t dpGetAdcRecordCount(void)
{
    return adcRecordTotal;
}

uint32_t dpGetForceRecordCount(void)
{
    return forceRecordTotal;
}

void dpFillImu(binForceRecord_t *rec)
{
    int16_t accel[3] = {0, 0, 0};
    int16_t gyro[3]  = {0, 0, 0};

    if (imuReadRaw(accel, gyro) == 0)
    {
        rec->accelX = accel[0];
        rec->accelY = accel[1];
        rec->accelZ = accel[2];
        rec->gyroX  = gyro[0];
        rec->gyroY  = gyro[1];
        rec->gyroZ  = gyro[2];
        rec->validity |= VALIDITY_IMU_OK;
    }
}
