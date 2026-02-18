#include "services/button.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static constexpr unsigned DEBOUNCE_MS = 25;
static constexpr unsigned SHORT_MIN_MS = 50;
static constexpr unsigned SHORT_MAX_MS = 350;
static constexpr unsigned LONG_MIN_MS = 1500;
static constexpr unsigned DOUBLE_GAP_MAX_MS = 350;

static int s_pin = -1;
static bool s_active_low = true;
static QueueHandle_t s_queue = nullptr;
static constexpr size_t QUEUE_LEN = 4;

// Raw debounced state: true = pressed (active)
static bool s_pressed = false;
static uint32_t s_last_change_ms = 0;
static bool s_last_raw = false;

// Gesture: press start, release time, last release for double
static bool s_press_started = false;
static uint32_t s_press_start_ms = 0;
static uint32_t s_last_release_ms = 0;
static bool s_waiting_second_press = false;
static bool s_double_just_emitted = false;

static bool read_pressed() {
  if (s_pin < 0) return false;
  int v = digitalRead(s_pin);
  return s_active_low ? (v == LOW) : (v == HIGH);
}

void button_begin(int gpio_pin, bool active_low) {
  s_pin = gpio_pin;
  s_active_low = active_low;
  pinMode(s_pin, INPUT_PULLUP);
  s_queue = xQueueCreate(QUEUE_LEN, sizeof(ButtonEvent));
  s_pressed = false;
  s_last_raw = read_pressed();
  s_last_change_ms = millis();
  s_press_started = false;
  s_waiting_second_press = false;
}

static void push_event(ButtonEvent e) {
  if (s_queue && e != ButtonEvent::NONE) {
    ButtonEvent ev = e;
    xQueueSend(s_queue, &ev, 0);
  }
}

void button_tick(uint32_t now_ms) {
  if (s_pin < 0) return;

  bool raw = read_pressed();
  if (raw != s_last_raw) {
    s_last_raw = raw;
    s_last_change_ms = now_ms;
  }

  bool stable = (now_ms - s_last_change_ms) >= DEBOUNCE_MS;
  bool now_pressed = stable ? raw : s_pressed;

  if (now_pressed != s_pressed) {
    s_pressed = now_pressed;
    if (s_pressed) {
      s_press_start_ms = now_ms;
      if (s_waiting_second_press && (now_ms - s_last_release_ms) <= DOUBLE_GAP_MAX_MS) {
        push_event(ButtonEvent::DOUBLE_PRESS);
        s_waiting_second_press = false;
        s_double_just_emitted = true;
      }
      s_press_started = true;
    } else {
      if (s_press_started) {
        uint32_t dur = now_ms - s_press_start_ms;
        if (dur >= LONG_MIN_MS)
          push_event(ButtonEvent::LONG_PRESS);
        else if (dur >= SHORT_MIN_MS && dur <= SHORT_MAX_MS) {
          if (s_double_just_emitted)
            s_double_just_emitted = false;
          else if (!s_waiting_second_press)
            s_waiting_second_press = true;
          else
            push_event(ButtonEvent::SHORT_PRESS);
        }
        s_press_started = false;
      }
      s_last_release_ms = now_ms;
    }
  }

  if (!s_pressed && s_waiting_second_press && (now_ms - s_last_release_ms) > DOUBLE_GAP_MAX_MS) {
    push_event(ButtonEvent::SHORT_PRESS);
    s_waiting_second_press = false;
  }
}

bool button_get_event(ButtonEvent* out) {
  if (!s_queue || !out) return false;
  return xQueueReceive(s_queue, out, 0) == pdTRUE;
}
