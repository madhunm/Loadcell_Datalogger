/**
 * @file    app_state.c
 * @brief   Application state machine — minimal stub for USB logging gate.
 * @details Phase 11 builds the full state machine.  This stub returns
 *          STATE_IDLE and gates logging based on USB connection status.
 *          Upstream: battery_monitor (USB sense).
 *          Downstream: main (button handler), logger (Phase 11).
 * @author  Madhu
 * @date    2026-04-12
 */

#include "app_state.h"
#include "battery_monitor.h"
#include <stdio.h>

/** @brief  Allow logging while USB is connected.  1 = dev mode (allow),
 *          0 = production (block).  Phase 10 loads from config.txt. */
static uint8_t allowLogOnUsb = 1;

appState_t appStateGet(void)
{
    return STATE_IDLE;
}

bool appStateCanStartLogging(void)
{
    if (allowLogOnUsb == 0 && batteryIsUsbConnected())
    {
        printf("[APP] logging BLOCKED: USB connected & allowLogOnUsb=0\r\n");
        return false;
    }
    return true;
}
