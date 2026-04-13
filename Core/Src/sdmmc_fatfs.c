/**
 * @file    sdmmc_fatfs.c
 * @brief   FatFS dual-file logging session implementation.
 * @details See sdmmc_fatfs.h.  Stall detection wraps f_write; USB stack is polled
 *          when a write exceeds 10 ms so enumeration stays healthy.
 * @author  Madhu
 * @date    2026-04-13
 */

#include "sdmmc_fatfs.h"
#include "adc_ads131m02.h"
#include "app_usbx.h"
#include "calibration.h"
#include "circular_buffer.h"
#include "debug_uart.h"
#include "diag_timers.h"
#include "log_record.h"
#include "osc_ltc6903.h"
#include "stm32h5xx_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_sdSessionSeq;

/**
 * @brief  After an f_write, update stall stats and optionally service USB.
 * @param[in,out] s   Session record.
 * @param[in]     t0  HAL_GetTick() before f_write.
 */
static void sdMeasureWriteStall(sdSession_t *s, uint32_t t0)
{
    uint32_t elapsed = HAL_GetTick() - t0;
    if (elapsed > s->stallMaxMs)
        s->stallMaxMs = elapsed;
    if (elapsed > 10U)
    {
        s->stallCount++;
        s->usbKeepaliveCount++;
        ux_system_tasks_run();
        cdcPoll();
    }
}

/**
 * @brief  Service USB during a long FatFS seek (prealloc) without touching write-stall stats.
 * @note   Exit qualification stall_max applies to f_write during streaming, not one-shot f_lseek.
 */
static void sdUsbKeepaliveIfSlow(uint32_t t0)
{
    if ((HAL_GetTick() - t0) > 10U)
    {
        ux_system_tasks_run();
        cdcPoll();
    }
}

/**
 * @brief  Fill and write the 64-byte binary file header.
 * @param[in,out] s  Open session (bin file positioned at 0).
 * @return FR_OK on success.
 */
static FRESULT sdWriteBinHeader(sdSession_t *s)
{
    const calConfig_t *cal = calibrationGet();
    binFileHeader_t    hdr;

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic           = BIN_FILE_MAGIC;
    hdr.version         = BIN_FORMAT_VER;
    hdr.headerSize      = sizeof(binFileHeader_t);
    hdr.clkinHz         = diagClkinGetHz();
    hdr.sysclkHz        = HAL_RCC_GetSysClockFreq();
    hdr.ltcDac          = ltc6903GetDac();
    hdr.adcOsr          = 128U;
    hdr.adcRecordRate   = 8000U;
    hdr.forceRecordRate = 500U;
    hdr.sensitivity     = cal->sensitivityUvPerN;
    hdr.tareOffset      = cal->tareOffsetN;
    hdr.adcGainCh1      = (uint8_t)cal->adcGainCh1;
    hdr.adcGainCh2      = (uint8_t)cal->adcGainCh2;
    hdr.imuOdr          = 200U;
    hdr.imuFsAccel      = 16U;
    hdr.rtcEpoch        = 0U;
    {
        static const uint8_t FWVER[8] = { 'v', '0', '.', '1', '1', '.', '0', '\0' };
        memcpy(hdr.fwVersion, FWVER, sizeof(FWVER));
    }
    hdr.serialNumber   = calibrationGetSerial();
    hdr.cellCorrFactor = cal->cellCorrFactor;
    hdr.crc16          = crc16Ccitt((const uint8_t *)&hdr, offsetof(binFileHeader_t, crc16));

    UINT bw = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT fr = f_write(&s->binFile, &hdr, sizeof(hdr), &bw);
    sdMeasureWriteStall(s, t0);
    if (fr != FR_OK || bw != sizeof(hdr))
        return (fr != FR_OK) ? fr : FR_INT_ERR;

    s->binBytesWritten += bw;
    return FR_OK;
}

/**
 * @brief  Write CSV comment header lines.
 * @param[in,out] s  Open session.
 * @return FR_OK on success.
 */
static FRESULT sdWriteCsvHeader(sdSession_t *s)
{
    static const char kHdr[] =
        "# H562 parachute datalogger\r\n"
        "# format: $,time_ms,load_N,#\r\n";

    UINT     bw = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT  fr = f_write(&s->csvFile, kHdr, sizeof(kHdr) - 1U, &bw);
    sdMeasureWriteStall(s, t0);
    if (fr != FR_OK || bw != sizeof(kHdr) - 1U)
        return (fr != FR_OK) ? fr : FR_INT_ERR;

    s->csvBytesWritten += bw;
    return FR_OK;
}

FRESULT sdSessionOpen(sdSession_t *s)
{
    char          pathBin[40];
    char          pathCsv[40];
    const calConfig_t *cal = calibrationGet();
    uint32_t      tag;
    FRESULT       fr;
    uint32_t      t0;
    FSIZE_t       preBin;
    FSIZE_t       preCsv;

    memset(s, 0, sizeof(*s));

    tag = (++g_sdSessionSeq << 16) ^ HAL_GetTick();
    snprintf(s->binFilename, sizeof(s->binFilename), "LOG_%08lX.bin", (unsigned long)tag);
    snprintf(s->csvFilename, sizeof(s->csvFilename), "LOG_%08lX.csv", (unsigned long)tag);

    snprintf(pathBin, sizeof(pathBin), "1:/%s", s->binFilename);
    snprintf(pathCsv, sizeof(pathCsv), "0:/%s", s->csvFilename);

    fr = f_open(&s->binFile, pathBin, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
        return fr;

    fr = f_open(&s->csvFile, pathCsv, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        f_close(&s->binFile);
        return fr;
    }

    s->isOpen = 1U;

    preBin = (FSIZE_t)((uint32_t)cal->preallocMb * 1024UL * 1024UL);
    preCsv = (FSIZE_t)((uint32_t)cal->preallocMb / 4U * 1024UL * 1024UL);
    if (preBin > 0U)
    {
        t0 = HAL_GetTick();
        fr = f_lseek(&s->binFile, preBin);
        sdUsbKeepaliveIfSlow(t0);
        if (fr != FR_OK)
        {
            f_close(&s->csvFile);
            f_close(&s->binFile);
            s->isOpen = 0U;
            return fr;
        }
        t0 = HAL_GetTick();
        fr = f_lseek(&s->binFile, 0);
        sdUsbKeepaliveIfSlow(t0);
        if (fr != FR_OK)
        {
            f_close(&s->csvFile);
            f_close(&s->binFile);
            s->isOpen = 0U;
            return fr;
        }
    }
    if (preCsv > 0U)
    {
        t0 = HAL_GetTick();
        fr = f_lseek(&s->csvFile, preCsv);
        sdUsbKeepaliveIfSlow(t0);
        if (fr != FR_OK)
        {
            f_close(&s->csvFile);
            f_close(&s->binFile);
            s->isOpen = 0U;
            return fr;
        }
        t0 = HAL_GetTick();
        fr = f_lseek(&s->csvFile, 0);
        sdUsbKeepaliveIfSlow(t0);
        if (fr != FR_OK)
        {
            f_close(&s->csvFile);
            f_close(&s->binFile);
            s->isOpen = 0U;
            return fr;
        }
    }

    s->sessionStartTick = HAL_GetTick();
    s->lastSyncTick     = s->sessionStartTick;
    s->adcPushBase      = g_adcPushCount;
    s->forcePushBase    = g_forcePushCount;
    s->missCountBase    = ads131m02GetStats()->missCount;
    s->overflowBase     = g_binRing.overflow;

    fr = sdWriteBinHeader(s);
    if (fr != FR_OK)
    {
        f_close(&s->csvFile);
        f_close(&s->binFile);
        s->isOpen = 0U;
        return fr;
    }

    fr = sdWriteCsvHeader(s);
    if (fr != FR_OK)
    {
        f_close(&s->csvFile);
        f_close(&s->binFile);
        s->isOpen = 0U;
        return fr;
    }

    printf("LOG START: 1:/%s + 0:/%s\r\n", s->binFilename, s->csvFilename);
    printf("LOG START: prealloc=%luMB bin, %luMB csv\r\n",
           (unsigned long)(uint32_t)cal->preallocMb,
           (unsigned long)((uint32_t)cal->preallocMb / 4U));

    return FR_OK;
}

FRESULT sdSessionWriteBinChunk(sdSession_t *s, const void *data, UINT len)
{
    if (!s->isOpen || len == 0U)
        return FR_OK;

    UINT     bw = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT  fr = f_write(&s->binFile, data, len, &bw);
    sdMeasureWriteStall(s, t0);
    if (fr != FR_OK)
        return fr;
    s->binBytesWritten += bw;
    return FR_OK;
}

FRESULT sdSessionWriteCsvChunk(sdSession_t *s, const void *data, UINT len)
{
    if (!s->isOpen || len == 0U)
        return FR_OK;

    UINT     bw = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT  fr = f_write(&s->csvFile, data, len, &bw);
    sdMeasureWriteStall(s, t0);
    if (fr != FR_OK)
        return fr;
    s->csvBytesWritten += bw;
    return FR_OK;
}

void sdSessionTrySync(sdSession_t *s)
{
    if (!s->isOpen)
        return;

    uint32_t now = HAL_GetTick();
    if ((now - s->lastSyncTick) < 10000U)
        return;

    uint32_t t0 = HAL_GetTick();
    f_sync(&s->binFile);
    f_sync(&s->csvFile);
    sdMeasureWriteStall(s, t0);
    s->lastSyncTick = HAL_GetTick();
    s->syncCount++;
}

FRESULT sdSessionClose(sdSession_t *s)
{
    FRESULT fr;

    if (!s->isOpen)
        return FR_OK;

    fr = f_lseek(&s->binFile, (FSIZE_t)s->binBytesWritten);
    if (fr == FR_OK)
        fr = f_truncate(&s->binFile);
    if (fr == FR_OK)
    {
        fr = f_lseek(&s->csvFile, (FSIZE_t)s->csvBytesWritten);
        if (fr == FR_OK)
            fr = f_truncate(&s->csvFile);
    }

    f_close(&s->csvFile);
    f_close(&s->binFile);
    s->isOpen = 0U;

    s->adcCount   = g_adcPushCount - s->adcPushBase;
    s->forceCount = g_forcePushCount - s->forcePushBase;

    uint32_t sessionMiss      = ads131m02GetStats()->missCount - s->missCountBase;
    uint32_t sessionOverflow  = g_binRing.overflow - s->overflowBase;
    uint32_t durS             = (HAL_GetTick() - s->sessionStartTick) / 1000U;
    uint32_t adcRate          = durS ? (s->adcCount / durS) : 0U;
    uint32_t forceRate        = durS ? (s->forceCount / durS) : 0U;
    uint32_t csvRate          = durS ? (s->csvCount / durS) : 0U;

    printf("LOG STOP: dur=%lus ADC=%lu(%lu/s) FORCE=%lu(%lu/s)"
           " META=%lu(%lu/s) CSV=%lu(%lu/s)\r\n",
           (unsigned long)durS,
           (unsigned long)s->adcCount, (unsigned long)adcRate,
           (unsigned long)s->forceCount, (unsigned long)forceRate,
           (unsigned long)s->metaCount, (unsigned long)(durS ? (s->metaCount / durS) : 0U),
           (unsigned long)s->csvCount, (unsigned long)csvRate);

    printf("LOG STOP: bin=%luB csv=%luB ovf=%lu miss=%lu"
           " stall_max=%lums stall_n=%lu\r\n",
           (unsigned long)s->binBytesWritten,
           (unsigned long)s->csvBytesWritten,
           (unsigned long)sessionOverflow,
           (unsigned long)sessionMiss,
           (unsigned long)s->stallMaxMs,
           (unsigned long)s->stallCount);

    printf("LOG STOP: ring_peak=%lu%% pressure_events=%lu"
           " usb_keepalive=%lu sync_count=%lu\r\n",
           (unsigned long)(s->ringPeakUsed * 100U / RING_BIN_SIZE),
           (unsigned long)s->pressureEvents,
           (unsigned long)s->usbKeepaliveCount,
           (unsigned long)s->syncCount);

    {
        bool pass = (sessionOverflow == 0U) && (sessionMiss == 0U)
                    && (adcRate >= 7900U && adcRate <= 8100U)
                    && (forceRate >= 490U && forceRate <= 510U)
                    && (csvRate >= 490U && csvRate <= 510U)
                    && (s->stallMaxMs < 200U)
                    && (s->ringPeakUsed < (RING_BIN_SIZE * 3U / 4U));

        if (pass)
        {
            printf("LOG STOP: PASS\r\n");
        }
        else
        {
            if (sessionOverflow != 0U)
                printf("LOG STOP: FAIL ovf=%lu\r\n", (unsigned long)sessionOverflow);
            if (sessionMiss != 0U)
                printf("LOG STOP: FAIL miss=%lu\r\n", (unsigned long)sessionMiss);
            if (adcRate < 7900U || adcRate > 8100U)
                printf("LOG STOP: FAIL adc_rate=%lu/s\r\n", (unsigned long)adcRate);
            if (forceRate < 490U || forceRate > 510U)
                printf("LOG STOP: FAIL force_rate=%lu/s\r\n", (unsigned long)forceRate);
            if (csvRate < 490U || csvRate > 510U)
                printf("LOG STOP: FAIL csv_rate=%lu/s\r\n", (unsigned long)csvRate);
            if (s->stallMaxMs >= 200U)
                printf("LOG STOP: FAIL stall_max=%lums\r\n", (unsigned long)s->stallMaxMs);
            if (s->ringPeakUsed >= (RING_BIN_SIZE * 3U / 4U))
                printf("LOG STOP: FAIL ring_peak=%lu%%\r\n",
                       (unsigned long)(s->ringPeakUsed * 100U / RING_BIN_SIZE));
        }
    }

    return FR_OK;
}
