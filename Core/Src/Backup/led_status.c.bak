/**
 * @file    led_status.c
 * @brief   High-level LED status engine — pattern timing and rotation logic.
 * @details Evaluates LED 0 priority table and LED 1 subsystem rotation every
 *          LED_UPDATE_INTERVAL_MS, applying IEC 60073 blink patterns.  All
 *          timing is software-driven from HAL_GetTick(); no ISR involvement.
 * @author  Madhu
 * @date    2026-04-12
 */

#include "led_status.h"
#include "neopixel.h"
#include "stm32h5xx_hal.h"
#include <stdio.h>

/* ── Colour palette (sunlight-readable, no pastels) ───────────── */
#define COL_RED_R       255u
#define COL_RED_G       0u
#define COL_RED_B       0u

#define COL_GREEN_R     0u
#define COL_GREEN_G     255u
#define COL_GREEN_B     0u

#define COL_BLUE_R      0u
#define COL_BLUE_G      0u
#define COL_BLUE_B      255u

#define COL_ORANGE_R    255u
#define COL_ORANGE_G    80u
#define COL_ORANGE_B    0u

/* ── LED 0 system-state look-up table ─────────────────────────── */

typedef struct {
    uint8_t      r;
    uint8_t      g;
    uint8_t      b;
    ledPattern_t pattern;
} ledEntry_t;

/** @brief  LED 0 colour/pattern for each ledSysState_t (index = enum value). */
static const ledEntry_t SYS_TABLE[] = {
    [LED_SYS_BOOT]      = { COL_RED_R,    COL_RED_G,    COL_RED_B,    LED_PATTERN_HEARTBEAT  },
    [LED_SYS_IDLE]       = { COL_RED_R,    COL_RED_G,    COL_RED_B,    LED_PATTERN_SOLID      },
    [LED_SYS_CHARGING]   = { COL_BLUE_R,   COL_BLUE_G,   COL_BLUE_B,   LED_PATTERN_SLOW_BLINK },
    [LED_SYS_BATT_LOW]   = { COL_ORANGE_R, COL_ORANGE_G, COL_ORANGE_B, LED_PATTERN_SOLID      },
    [LED_SYS_BATT_CRIT]  = { COL_ORANGE_R, COL_ORANGE_G, COL_ORANGE_B, LED_PATTERN_FAST_BLINK },
    [LED_SYS_ERROR]      = { COL_RED_R,    COL_RED_G,    COL_RED_B,    LED_PATTERN_FAST_BLINK },
};

/* ── Module state ─────────────────────────────────────────────── */

static ledSysState_t  sysState;
static ledSubLevel_t  subLevels[LED_SUB_COUNT];
static bool           loggingActive;
static bool           initialised;

/* Rotation engine — tracks which subsystems are in the active rotation */
static uint32_t       rotateEpoch;
static uint8_t        lastActiveMask;

/* ── Pattern evaluation ───────────────────────────────────────── */

/**
 * @brief  Evaluate a blink pattern and drive a single LED pixel.
 * @param[in] ledIdx   Pixel index (0 or 1).
 * @param[in] r,g,b    Colour when ON.
 * @param[in] pattern  Blink mode to apply.
 * @param[in] now      Current HAL_GetTick() value.
 */
static void applyPattern(uint8_t ledIdx, uint8_t r, uint8_t g, uint8_t b,
                          ledPattern_t pattern, uint32_t now)
{
    bool on = false;

    switch (pattern)
    {
    case LED_PATTERN_OFF:
        break;

    case LED_PATTERN_SOLID:
        on = true;
        break;

    case LED_PATTERN_SLOW_BLINK:
        on = ((now % (LED_SLOW_BLINK_MS * 2u)) < LED_SLOW_BLINK_MS);
        break;

    case LED_PATTERN_FAST_BLINK:
        on = ((now % (LED_FAST_BLINK_MS * 2u)) < LED_FAST_BLINK_MS);
        break;

    case LED_PATTERN_HEARTBEAT:
        on = ((now % LED_HEARTBEAT_PERIOD_MS) < LED_HEARTBEAT_ON_MS);
        break;
    }

    if (on)
        neoSetPixel(ledIdx, r, g, b);
    else
        neoSetPixel(ledIdx, 0, 0, 0);
}

/* ── LED 0 update (system health) ─────────────────────────────── */

static void updateLed0(uint32_t now)
{
    const ledEntry_t *e = &SYS_TABLE[sysState];
    applyPattern(0, e->r, e->g, e->b, e->pattern, now);
}

/* ── LED 1 update (subsystems + logging) ──────────────────────── */

static void updateLed1(uint32_t now)
{
    /* Build list of non-OK subsystems */
    int  badIdx[LED_SUB_COUNT];
    int  badCount = 0;

    for (int i = 0; i < (int)LED_SUB_COUNT; i++)
    {
        if (subLevels[i] != LED_LEVEL_OK)
            badIdx[badCount++] = i;
    }

    /* ── Case 1: all subsystems OK ── */
    if (badCount == 0)
    {
        if (loggingActive)
            applyPattern(1, COL_GREEN_R, COL_GREEN_G, COL_GREEN_B,
                         LED_PATTERN_SOLID, now);
        else
            applyPattern(1, COL_GREEN_R, COL_GREEN_G, COL_GREEN_B,
                         LED_PATTERN_HEARTBEAT, now);

        rotateEpoch    = now;
        lastActiveMask = 0;
        return;
    }

    /* ── Case 2: warnings/errors present — rotation engine ── */

    /* Compute a bitmask for change detection */
    uint8_t activeMask = 0;
    for (int i = 0; i < badCount; i++)
        activeMask |= (uint8_t)(1u << badIdx[i]);
    if (loggingActive)
        activeMask |= (uint8_t)(1u << LED_SUB_COUNT);

    if (activeMask != lastActiveMask)
    {
        rotateEpoch    = now;
        lastActiveMask = activeMask;
    }

    int      totalSlots   = badCount + (loggingActive ? 1 : 0);
    uint32_t slotDuration = LED_ROTATE_HOLD_MS + LED_ROTATE_GAP_MS;
    uint32_t elapsed      = now - rotateEpoch;
    int      slotIndex    = (int)((elapsed / slotDuration) % (uint32_t)totalSlots);
    uint32_t slotPhase    = elapsed % slotDuration;
    bool     inGap        = (slotPhase >= LED_ROTATE_HOLD_MS);

    if (inGap)
    {
        neoSetPixel(1, 0, 0, 0);
        return;
    }

    /* If logging slot comes last in the rotation */
    if (loggingActive && slotIndex == totalSlots - 1)
    {
        applyPattern(1, COL_GREEN_R, COL_GREEN_G, COL_GREEN_B,
                     LED_PATTERN_SOLID, now);
        return;
    }

    /* Subsystem slot */
    ledSubLevel_t level = subLevels[badIdx[slotIndex]];
    if (level == LED_LEVEL_WARN)
        applyPattern(1, COL_BLUE_R, COL_BLUE_G, COL_BLUE_B,
                     LED_PATTERN_SLOW_BLINK, now);
    else
        applyPattern(1, COL_RED_R, COL_RED_G, COL_RED_B,
                     LED_PATTERN_FAST_BLINK, now);
}

/* ── Public API ───────────────────────────────────────────────── */

void ledStatusInit(void)
{
    sysState      = LED_SYS_BOOT;
    loggingActive = false;
    initialised   = true;
    rotateEpoch   = HAL_GetTick();
    lastActiveMask = 0;

    for (int i = 0; i < (int)LED_SUB_COUNT; i++)
        subLevels[i] = LED_LEVEL_OK;

    /* Immediate first frame: LED 0 = RED HEARTBEAT, LED 1 = OFF */
    neoSetPixel(0, COL_RED_R, COL_RED_G, COL_RED_B);
    neoSetPixel(1, 0, 0, 0);
    neoShow();
}

void ledStatusUpdate(void)
{
    if (!initialised)
        return;

    uint32_t now = HAL_GetTick();

    updateLed0(now);
    updateLed1(now);
    neoShow();
}

void ledStatusSetSys(ledSysState_t state)
{
    sysState = state;
}

void ledStatusSetSub(ledSubSystem_t sys, ledSubLevel_t level)
{
    if (sys < LED_SUB_COUNT)
        subLevels[sys] = level;
}

void ledStatusSetLogging(bool active)
{
    loggingActive = active;
}

const char *ledStatusGetDiagStr(void)
{
    static const char * const SYS_NAMES[] = {
        [LED_SYS_BOOT]      = "BOOT",
        [LED_SYS_IDLE]      = "IDLE",
        [LED_SYS_CHARGING]  = "CHRG",
        [LED_SYS_BATT_LOW]  = "BLOW",
        [LED_SYS_BATT_CRIT] = "BCRT",
        [LED_SYS_ERROR]     = "ERR",
    };
    static const char LEVEL_CH[] = { 'O', 'W', 'E' };
    static char buf[48];

    const char *sn = ((unsigned)sysState < sizeof(SYS_NAMES) / sizeof(SYS_NAMES[0]))
                     ? SYS_NAMES[sysState] : "?";
    snprintf(buf, sizeof(buf), "sys=%s sub=%c/%c/%c/%c log=%u",
             sn,
             LEVEL_CH[subLevels[LED_SUB_ADC]],
             LEVEL_CH[subLevels[LED_SUB_IMU]],
             LEVEL_CH[subLevels[LED_SUB_SD]],
             LEVEL_CH[subLevels[LED_SUB_LOGGER]],
             (unsigned)loggingActive);
    return buf;
}
