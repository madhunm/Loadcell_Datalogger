/**
 * @file    calibration.c
 * @brief   config.txt parser and calibration store.
 * @details Reads key=value pairs from "0:config.txt" via FatFS f_read()
 *          (FF_USE_STRFUNC is 0 in this project, so f_gets is unavailable).
 *          The entire file (up to 1 KB) is bulk-read into a stack buffer
 *          and split into lines manually.  Unknown keys are silently ignored
 *          (forward-compatible).  Missing keys retain hardcoded defaults.
 *          Every parsed key is printed to the serial console for operator
 *          verification.
 * @author  Madhu
 * @date    2026-04-12
 */

#include "calibration.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Hardcoded defaults ───────────────────────────────────────────── */

static calConfig_t g_cal = {
    .sensitivityUvPerN = 2.0f,
    .adcGainCh1        = 1,
    .adcGainCh2        = 1,
    .offsetCh1         = 0,
    .offsetCh2         = 0,
    .tareOffsetN       = 0.0f,
    .battDividerRatio  = 0.5f,
    .preallocMb        = 64,
    .enableAdcCrc      = 0,
    .allowLogOnUsb     = 1,
};

static calSource_t g_calSource = CAL_SRC_DEFAULT;

/* ── Helpers ──────────────────────────────────────────────────────── */

/** @brief Strip leading/trailing whitespace in-place; return pointer to first non-space. */
static char *trimWhitespace(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';
    return s;
}

/** @brief Skip UTF-8 BOM (EF BB BF) if present at start of buffer. */
static char *skipBom(char *s)
{
    if ((uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF)
        return s + 3;
    return s;
}

#define CAL_TOTAL_KEYS  10

/**
 * @brief  Try to match and apply a single key=value pair.
 * @return 1 if the key was recognised and applied, 0 otherwise.
 */
static int applyKeyValue(const char *key, const char *val)
{
    if      (strcmp(key, "sensitivityUvPerN") == 0) { g_cal.sensitivityUvPerN = strtof(val, NULL); return 1; }
    else if (strcmp(key, "adcGainCh1")        == 0) { g_cal.adcGainCh1       = (uint8_t)strtol(val, NULL, 10); return 1; }
    else if (strcmp(key, "adcGainCh2")        == 0) { g_cal.adcGainCh2       = (uint8_t)strtol(val, NULL, 10); return 1; }
    else if (strcmp(key, "offsetCh1")          == 0) { g_cal.offsetCh1        = (int32_t)strtol(val, NULL, 10); return 1; }
    else if (strcmp(key, "offsetCh2")          == 0) { g_cal.offsetCh2        = (int32_t)strtol(val, NULL, 10); return 1; }
    else if (strcmp(key, "tareOffsetN")        == 0) { g_cal.tareOffsetN      = strtof(val, NULL); return 1; }
    else if (strcmp(key, "battDividerRatio")   == 0) { g_cal.battDividerRatio = strtof(val, NULL); return 1; }
    else if (strcmp(key, "preallocMb")         == 0) { g_cal.preallocMb       = (uint32_t)strtoul(val, NULL, 10); return 1; }
    else if (strcmp(key, "enableAdcCrc")       == 0) { g_cal.enableAdcCrc     = (uint8_t)strtol(val, NULL, 10); return 1; }
    else if (strcmp(key, "allowLogOnUsb")      == 0) { g_cal.allowLogOnUsb    = (uint8_t)strtol(val, NULL, 10); return 1; }
    return 0;
}

/* ── Public API ───────────────────────────────────────────────────── */

/** @brief Max config.txt size we support (stack-allocated read buffer). */
#define CAL_FILE_MAX  1024

/**
 * @brief  Parse a NUL-terminated buffer of key=value lines.
 * @return Number of recognised keys applied.
 */
static int parseBuffer(char *buf, UINT len)
{
    int loaded   = 0;
    int firstLine = 1;
    char *cursor = buf;
    char *end    = buf + len;

    while (cursor < end)
    {
        /* Find end of current line (\n or \r\n or end-of-buffer) */
        char *eol = cursor;
        while (eol < end && *eol != '\n' && *eol != '\r')
            eol++;

        /* NUL-terminate this line */
        if (eol < end)
            *eol = '\0';

        char *p = cursor;

        /* Advance cursor past line terminator(s) for next iteration */
        cursor = eol + 1;
        if (cursor < end && *(cursor) == '\n' && *(eol) == '\0' && eol > buf && *(eol - 1) != '\n')
        {
            /* Handle \r\n: the \r was already NUL'd; skip the \n too */
        }
        if (cursor < end && *cursor == '\n' && eol > buf)
            cursor++;

        /* Skip BOM on the very first line */
        if (firstLine)
        {
            p = skipBom(p);
            firstLine = 0;
        }

        p = trimWhitespace(p);

        if (*p == '\0' || *p == '#' || *p == ';')
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = trimWhitespace(p);
        char *val = trimWhitespace(eq + 1);

        if (applyKeyValue(key, val))
        {
            printf("CAL: %s = %s\r\n", key, val);
            loaded++;
        }
    }

    return loaded;
}

int calibrationLoad(void)
{
    FIL    fil;
    char   buf[CAL_FILE_MAX];
    UINT   bytesRead = 0;
    int    loaded = 0;

    FRESULT fr = f_open(&fil, "0:config.txt", FA_READ);
    if (fr != FR_OK)
    {
        printf("CAL: config.txt not found (FR=%d), using defaults\r\n", (int)fr);
        g_calSource = CAL_SRC_DEFAULT;
        return 0;
    }

    fr = f_read(&fil, buf, sizeof(buf) - 1, &bytesRead);
    f_close(&fil);

    if (fr != FR_OK || bytesRead == 0)
    {
        printf("CAL: config.txt read failed (FR=%d, %u bytes), using defaults\r\n",
               (int)fr, (unsigned)bytesRead);
        g_calSource = CAL_SRC_DEFAULT;
        return 0;
    }

    buf[bytesRead] = '\0';
    loaded = parseBuffer(buf, bytesRead);

    if (loaded > 0)
    {
        g_calSource = CAL_SRC_SD_FILE;
        printf("CAL: loaded %d/%d keys from SD\r\n", loaded, CAL_TOTAL_KEYS);
    }
    else
    {
        g_calSource = CAL_SRC_DEFAULT;
        printf("CAL: WARNING — config.txt exists but no keys parsed\r\n");
    }

    return loaded;
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
