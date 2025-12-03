#include "rtc.h"

#include "RX8900.h"
#include "pins.h"

static RX8900 rtcDevice;
static volatile bool rtcUpdatePending = false;

void IRAM_ATTR rtcIntIsr()
{
    rtcUpdatePending = true;
}

bool rtcInit()
{
    Serial.println("[INIT][RTC] Initialising RX8900...");

    pinMode(PIN_RTC_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_RTC_INT), rtcIntIsr, FALLING);

    rtcDevice.RX8900Init();

    // false = 1-second update, true = 1-minute update (per library example)
    rtcDevice.UpdateInterruptTimingChange(false);

    // InterruptSettings(AIE, TIE, UIE) – only enable Update Interrupt
    rtcDevice.InterruptSettings(false, false, true);

    Serial.println("[INIT][RTC] RX8900 configured for 1 Hz update interrupt.");
    return true;
}

void rtcHandleUpdate()
{
    if (!rtcUpdatePending)
        return;

    rtcUpdatePending = false;

    uint8_t flagChange = 0;
    rtcDevice.JudgeInterruptSignalType(&flagChange);

    Serial.print("[RTC] flagChange: 0b");
    Serial.println(flagChange, BIN);

    // Clear only the Update Flag (UF). AF/TF remain untouched.
    rtcDevice.ClearOccurrenceNotification(false, false, true);
}

RX8900 &rtcGetDevice()
{
    return rtcDevice;
}
