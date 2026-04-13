/**
 * @file    app_state.c
 * @brief   Application state machine implementation.
 * @author  Madhu
 * @date    2026-04-13
 */

#include "app_state.h"
#include "battery_monitor.h"
#include "calibration.h"
#include <stdio.h>

static appState_t        g_appState = STATE_IDLE;
static volatile uint8_t g_buttonLatch = 0U;

appState_t appStateGet(void)
{
    return g_appState;
}

void appStateSet(appState_t state)
{
    g_appState = state;
}

bool appStateCanStartLogging(void)
{
    if (calibrationGet()->allowLogOnUsb == 0U && batteryIsUsbConnected())
    {
        printf("[APP] logging BLOCKED: USB connected & allowLogOnUsb=0\r\n");
        return false;
    }
    return true;
}

void appStateButtonIsr(void)
{
    g_buttonLatch = 1U;
}

bool appStateConsumeButtonPress(void)
{
    if (g_buttonLatch == 0U)
        return false;
    g_buttonLatch = 0U;
    return true;
}
