#pragma once
#include <cstdint>

// Single WS2812; saturated colors only; default brightness 96/255.
// Priority: FAULT > WARNING > STATE. Call led_tick(now_ms) from loop/task; no delay().

enum class LedColor : uint8_t {
  OFF, RED, GREEN, BLUE, CYAN, YELLOW, MAGENTA, PURPLE, ORANGE
};

enum class UiState : uint8_t {
  BOOT,
  IDLE_READY,
  LOGGING,
  STOPPED,
  EXPORTING,
  FAULT,
};

// Fault types for N_BLINK patterns (2=SD missing, 3=SD write, 4=ADC, 5=IMU, etc.)
enum class LedFault : uint8_t {
  NONE = 0,
  SD_MOUNT = 2,
  SD_WRITE = 3,
  ADC_FAULT = 4,
  IMU_FAULT = 5,
};

enum class LedWarning : uint8_t {
  NONE = 0,
  RTC_INVALID,
  RTC_FAULT,   // RTC read failed
  LOW_BATT,
  BATT_WARN,
  UNDERLOAD,
  OVERLOAD,
  COMPRESSION,
  IMU_WARN,  // optional 2-blink magenta when IMU not mandatory fault
};

void led_begin(int gpio_pin, int num_pixels = 1);
void led_tick(uint32_t now_ms);

void led_set_state(UiState s);
void led_set_fault(LedFault f);
void led_set_warning(LedWarning w);
void led_clear_fault();
void led_clear_warning();

// Reserved for future use
void led_set_custom1(uint8_t r, uint8_t g, uint8_t b);
void led_set_custom2(uint8_t r, uint8_t g, uint8_t b);
