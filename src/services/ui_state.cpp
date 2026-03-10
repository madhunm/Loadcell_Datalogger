#include "services/ui_state.h"
#include "services/button.h"
#include "services/led_ui.h"
#include "services/sd_logger.h"
#include "services/frame_pipe.h"
#include "services/aux_state.h"
#include "services/system_status.h"
#include "services/sensors_task.h"
#include "pins.h"
#include <Arduino.h>

static UiState s_ui_state = UiState::BOOT;
static uint32_t s_warning_blink_until_ms = 0;

void ui_init() {
  button_begin(PIN_LOG_BUTTON, true);
  led_begin(PIN_NEOPIXEL, 1);
  s_ui_state = UiState::BOOT;
  led_set_state(UiState::BOOT);
  s_warning_blink_until_ms = 0;

  if (logger_begin()) {
    s_ui_state = UiState::IDLE_READY;
    led_set_state(UiState::IDLE_READY);
  } else {
    s_ui_state = UiState::FAULT;
  }
}

static void do_start_log() {
  AuxSnapshot snap = aux_get_snapshot();
  PdlHeaderV1 hdr = logger_make_default_header();
  if (snap.accel_g_per_lsb > 0.f && snap.gyro_dps_per_lsb > 0.f) {
    hdr.accel_g_per_lsb = snap.accel_g_per_lsb;
    hdr.gyro_dps_per_lsb = snap.gyro_dps_per_lsb;
  }
  if (logger_start_session(hdr, snap.rtc_valid, snap.rtc_epoch)) {
    s_ui_state = UiState::LOGGING;
    led_set_state(UiState::LOGGING);
  } else if (!logger_can_start()) {
    s_warning_blink_until_ms = millis() + 600;
  }
}

static void do_stop_log() {
  logger_stop_session();
  s_ui_state = UiState::FINALIZING;
  led_set_state(UiState::FINALIZING);
}

static void do_export_latest() {
  if (logger_is_busy()) {
    s_warning_blink_until_ms = millis() + 800;
    return;
  }
  if (!logger_has_last_bin()) {
    s_warning_blink_until_ms = millis() + 800;
    return;
  }
  s_ui_state = UiState::EXPORTING;
  led_set_state(UiState::EXPORTING);
  if (logger_export_latest_to_csv()) {
    s_ui_state = UiState::IDLE_READY;
    led_set_state(UiState::IDLE_READY);
  } else {
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    s_ui_state = UiState::IDLE_READY;
    led_set_state(UiState::IDLE_READY);
  }
}

static void do_retry_init() {
  if (logger_begin()) {
    system_status_clear_fault(FaultCode::SD_MOUNT_FAIL);
    system_status_clear_fault(FaultCode::SD_WRITE_FAIL);
  }
  sensors_request_retry_probe();
  if (system_status_get_led_fault() == LedFault::NONE) {
    s_ui_state = UiState::IDLE_READY;
    led_set_state(UiState::IDLE_READY);
  }
}

void ui_tick(uint32_t now_ms) {
  button_tick(now_ms);
  ButtonEvent ev;
  while (button_get_event(&ev)) {
    switch (s_ui_state) {
      case UiState::IDLE_READY:
        if (ev == ButtonEvent::SHORT_PRESS) do_start_log();
        else if (ev == ButtonEvent::LONG_PRESS) do_export_latest();
        else if (ev == ButtonEvent::DOUBLE_PRESS) { /* SELF_TEST no-op */ }
        break;
      case UiState::LOGGING:
        if (ev == ButtonEvent::LONG_PRESS) do_stop_log();
        else if (ev == ButtonEvent::SHORT_PRESS) frame_pipe_set_mark_next();
        break;
      case UiState::FINALIZING:
        break;
      case UiState::STOPPED:
        if (ev == ButtonEvent::SHORT_PRESS) do_start_log();
        else if (ev == ButtonEvent::LONG_PRESS) do_export_latest();
        break;
      case UiState::EXPORTING:
        if (ev == ButtonEvent::SHORT_PRESS) { /* CANCEL_EXPORT - already done when export runs */ }
        break;
      case UiState::FAULT:
        if (ev == ButtonEvent::SHORT_PRESS) do_retry_init();
        break;
      default:
        break;
    }
  }

  if (s_warning_blink_until_ms && now_ms < s_warning_blink_until_ms) {
    uint32_t pos = (now_ms % 400);
    if (pos < 80) led_set_custom1(96, 96, 0);
    else led_set_custom1(0, 0, 0);
  } else {
    s_warning_blink_until_ms = 0;
    if (s_ui_state == UiState::FINALIZING && !logger_is_busy()) {
      s_ui_state = UiState::STOPPED;
      led_set_state(UiState::STOPPED);
    }
    LedFault f = system_status_get_led_fault();
    LedWarning w = system_status_get_led_warning();
    if (f != LedFault::NONE) {
      s_ui_state = UiState::FAULT;
      led_set_fault(f);
      led_set_state(UiState::FAULT);
    } else {
      led_clear_fault();
      if (s_ui_state == UiState::FAULT) s_ui_state = UiState::IDLE_READY;
      led_set_state(s_ui_state);
    }
    if (w != LedWarning::NONE) led_set_warning(w);
    else led_clear_warning();
    led_tick(now_ms);
  }
}
