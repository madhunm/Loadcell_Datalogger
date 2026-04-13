/**
 * @file    calibration.c
 * @brief   Binary .cal loader, SYSCAL directory scan, and calibration store.
 * @details Reads packed calFile_t from "1:<serial>.cal", validates CRC16-CCITT
 *          over bytes 0..61, and populates calConfig_t.  scan/filename parsing
 *          uses FatFS f_findfirst/f_findnext.  Serial number is module-private.
 * @author  Madhu
 * @date    2026-04-13
 */

#include "calibration.h"
#include "app_state.h"
#include "debug_ui.h"
#include "led_status.h"
#include "log_record.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Defaults (restored on load failure) ─────────────────────────── */

static const calConfig_t kCalDefaults = {
    .sensitivityUvPerN = 2.0f,
    .adcGainCh1        = 1.0f,
    .adcGainCh2        = 1.0f,
    .offsetCh1         = 0,
    .offsetCh2         = 0,
    .tareOffsetN       = 0.0f,
    .battDividerRatio  = 0.5f,
    .preallocMb        = 64.0f,
    .enableAdcCrc      = 0,
    .allowLogOnUsb     = 1,
    .cellCorrFactor    = 1.0f,
};

static calConfig_t g_cal = {
    .sensitivityUvPerN = 2.0f,
    .adcGainCh1        = 1.0f,
    .adcGainCh2        = 1.0f,
    .offsetCh1         = 0,
    .offsetCh2         = 0,
    .tareOffsetN       = 0.0f,
    .battDividerRatio  = 0.5f,
    .preallocMb        = 64.0f,
    .enableAdcCrc      = 0,
    .allowLogOnUsb     = 1,
    .cellCorrFactor    = 1.0f,
};
static calSource_t  g_calSource = CAL_SRC_DEFAULT;
static uint32_t     g_calSerial = 0U;

static calEntry_t   g_calEntries[CAL_MAX_CELLS];
static uint8_t      g_calEntryCount = 0;

/* ── Public API ───────────────────────────────────────────────────── */

int calibrationLoad(void)
{
    /* Boot orchestration: main.c calls calScanFiles, calSelectViaUi,
     * calibrationLoadFromCal. */
    return 0;
}

uint32_t calibrationGetSerial(void)
{
    return g_calSerial;
}

void calibrationGetEntries(const calEntry_t **entries, uint8_t *count)
{
    if (entries != NULL)
        *entries = g_calEntries;
    if (count != NULL)
        *count = g_calEntryCount;
}

void calScanFiles(void)
{
    DIR     dir;
    FILINFO fno;
    uint8_t n = 0;

    memset(g_calEntries, 0, sizeof(g_calEntries));
    g_calEntryCount = 0;

    FRESULT fr = f_findfirst(&dir, &fno, "1:", "*.cal");
    while (fr == FR_OK && fno.fname[0] != '\0' && n < CAL_MAX_CELLS)
    {
        g_calEntries[n].serial = (uint32_t)strtoul(fno.fname, NULL, 10);
        strncpy(g_calEntries[n].filename, fno.fname,
                sizeof(g_calEntries[n].filename) - 1);
        g_calEntries[n].filename[sizeof(g_calEntries[n].filename) - 1] = '\0';
        n++;
        fr = f_findnext(&dir, &fno);
    }
    f_closedir(&dir);
    g_calEntryCount = n;
}

calSource_t calibrationGetSource(void)
{
    return g_calSource;
}

const calConfig_t *calibrationGet(void)
{
    return &g_cal;
}

void calibrationSetTare(float tareN)
{
    g_cal.tareOffsetN = tareN;
    printf("CAL: tare set to %.3f N\r\n", (double)tareN);
}

/**
 * @brief  Copy factory defaults into the active calibration buffer.
 */
static void calRestoreDefaults(void)
{
    memcpy(&g_cal, &kCalDefaults, sizeof(g_cal));
}

/**
 * @brief  Handle a failed calibration load: defaults, fault state, UI/LED.
 * @param[in] msg  UART / panel message.
 */
static void calFail(const char *msg)
{
    calRestoreDefaults();
    g_calSource = CAL_SRC_DEFAULT;
    g_calSerial = 0U;
    appStateSet(STATE_ERROR);
    ledStatusSetSys(LED_SYS_ERROR);
    printf("%s\r\n", msg);
    uiLog("%s", msg);
}

int calibrationLoadFromCal(uint32_t serialNumber)
{
    calFile_t cf;
    FIL       fil;
    UINT      br = 0;
    char      path[32];

    if (serialNumber == 0U)
    {
        calFail("[CAL] FAULT: no cell selected or invalid serial");
        return -1;
    }

    snprintf(path, sizeof(path), "1:%lu.cal", (unsigned long)serialNumber);

    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK)
    {
        printf("[CAL] FAULT: cannot open %s (FR=%d)\r\n", path, (int)fr);
        calFail("[CAL] FAULT: missing or unreadable .cal file");
        return -2;
    }

    fr = f_read(&fil, &cf, sizeof(cf), &br);
    f_close(&fil);

    if (fr != FR_OK || br != sizeof(cf))
    {
        printf("[CAL] FAULT: read %s bytes=%u (FR=%d)\r\n",
               path, (unsigned)br, (int)fr);
        calFail("[CAL] FAULT: .cal read error");
        return -3;
    }

    if (cf.magic != CAL_FILE_MAGIC)
    {
        printf("[CAL] FAULT: bad magic 0x%08lX\r\n", (unsigned long)cf.magic);
        calFail("[CAL] FAULT: invalid .cal magic");
        return -4;
    }

    if (cf.version != CAL_FILE_VER)
    {
        printf("[CAL] FAULT: bad version %u\r\n", (unsigned)cf.version);
        calFail("[CAL] FAULT: unsupported .cal version");
        return -5;
    }

    /* CRC16-CCITT over file bytes 0..61 (excludes crc16 at offset 62..63). */
    uint16_t crcCalc = crc16Ccitt((const uint8_t *)&cf, 62U);

    if (crcCalc != cf.crc16)
    {
        printf("[CAL] FAULT: CRC mismatch (calc 0x%04X, file 0x%04X)\r\n",
               (unsigned)crcCalc, (unsigned)cf.crc16);
        calFail("[CAL] FAULT: .cal CRC failed");
        return -6;
    }

    calRestoreDefaults();

    g_cal.sensitivityUvPerN = cf.sensitivityUvPerN;
    g_cal.adcGainCh1        = cf.adcGainCh1;
    g_cal.adcGainCh2        = cf.adcGainCh2;
    g_cal.offsetCh1         = (int32_t)cf.offsetCh1;
    g_cal.offsetCh2         = (int32_t)cf.offsetCh2;
    g_cal.tareOffsetN       = cf.tareOffsetN;
    g_cal.battDividerRatio  = cf.battDividerRatio;
    g_cal.preallocMb        = cf.preallocMb;
    g_cal.enableAdcCrc      = (uint8_t)cf.enableAdcCrc;
    g_cal.allowLogOnUsb     = (uint8_t)cf.allowLogOnUsb;
    g_cal.cellCorrFactor    = cf.cellCorrFactor;

    g_calSerial   = cf.serialNumber;
    g_calSource   = CAL_SRC_SD_FILE;

    printf("[CAL] loaded SN=%lu sens=%.6f G0=%.1f G1=%.1f off=%ld/%ld "
           "tare=%.3f div=%.3f pre=%.1f crcEn=%u usb=%u corr=%.6f\r\n",
           (unsigned long)g_calSerial,
           (double)g_cal.sensitivityUvPerN,
           (double)g_cal.adcGainCh1,
           (double)g_cal.adcGainCh2,
           (long)g_cal.offsetCh1,
           (long)g_cal.offsetCh2,
           (double)g_cal.tareOffsetN,
           (double)g_cal.battDividerRatio,
           (double)g_cal.preallocMb,
           (unsigned)g_cal.enableAdcCrc,
           (unsigned)g_cal.allowLogOnUsb,
           (double)g_cal.cellCorrFactor);

    return 0;
}
