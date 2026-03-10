#pragma once
#include <cstdint>

// Debounce 25 ms; short 50-350 ms; long >= 1500 ms; double = two short with gap <= 350 ms.
enum class ButtonEvent : uint8_t {
  NONE = 0,
  SHORT_PRESS,
  LONG_PRESS,
  DOUBLE_PRESS,
};

void button_begin(int gpio_pin, bool active_low = true);
void button_tick(uint32_t now_ms);
bool button_get_event(ButtonEvent* out);
