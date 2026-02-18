#include "services/ui_state.h"
#include "services/button.h"
#include "services/led_ui.h"
#include "services/sd_logger.h"
#include "services/frame_pipe.h"
#include "services/aux_state.h"
#include <Arduino.h>

static constexpr int BUTTON_GPIO = 0;
static constexpr int LED_GPIO = 2;

static UiState s_ui_state = UiState::BOOT;
static uint32_t s_warning_blink_until_ms = 0;

static void apply_led_state() {
  led_set_state(s_ui_state);
  if (s_ui_state == UiState::FAULT) {
    led_set_fault(LedFault::SD_MOUNT);
  } else {
    led_clear_fault();
  }
}

void ui_init() {
  button_begin(BUTTON_GPIO, true);
  led_begin(LED_GPIO, 1);
  s_ui_state = UiState::BOOT;
  led_set_state(UiState::BOOT);
  s_warning_blink_until_ms = 0;

  if (logger_begin()) {
    s_ui_state = UiState::IDLE_READY;
    led_set_state(UiState::IDLE_READY);
    led_clear_fault();
  } else {
    s_ui_state = UiState::FAULT;
    led_set_fault(LedFault::SD_MOUNT);
  }
}

static void do_start_log() {
  AuxSnapshot snap = aux_get_snapshot();
  PdlHeaderV1 hdr = logger_make_default_header();
  if (logger_start_session(hdr, snap.rtc_valid, snap.rtc_epoch)) {
    s_ui_state = UiState::LOGGING;
    led_set_state(UiState::LOGGING);
  }
}

static void do_stop_log() {
  logger_stop_session();
  s_ui_state = UiState::STOPPED;
  led_set_state(UiState::STOPPED);
}

static void do_export_latest() {
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
    led_set_fault(LedFault::SD_WRITE);
    s_ui_state = UiState::IDLE_READY;
    led_set_state(UiState::IDLE_READY);
    led_clear_fault();
  }
}

static void do_retry_init() {
  led_clear_fault();
  if (logger_begin()) {
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
    led_tick(now_ms);
  }
}
