#include "services/led_ui.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

static constexpr uint8_t DEFAULT_BRIGHTNESS = 96;
static constexpr int PIXELS = 1;

static Adafruit_NeoPixel* s_strip = nullptr;
static int s_pin = -1;

static UiState s_state = UiState::BOOT;
static LedFault s_fault = LedFault::NONE;
static LedWarning s_warning = LedWarning::NONE;

static uint32_t s_phase_start_ms = 0;
static uint8_t s_blink_count = 0;
static bool s_stopped_double_done = false;

static void get_color_rgb(LedColor c, uint8_t brightness, uint8_t* r, uint8_t* g, uint8_t* b) {
  uint8_t br = brightness;
  switch (c) {
    case LedColor::OFF:   *r=0; *g=0; *b=0; return;
    case LedColor::RED:   *r=br; *g=0; *b=0; return;
    case LedColor::GREEN: *r=0; *g=br; *b=0; return;
    case LedColor::BLUE:  *r=0; *g=0; *b=br; return;
    case LedColor::CYAN:  *r=0; *g=br; *b=br; return;
    case LedColor::YELLOW:*r=br; *g=br; *b=0; return;
    case LedColor::MAGENTA:*r=br;*g=0; *b=br; return;
    case LedColor::PURPLE:*r=br;*g=0; *b=(br*3/4); return; // saturated purple
    case LedColor::ORANGE:*r=br; *g=(uint8_t)((60*(uint16_t)br)/255); *b=0; return; // saturated orange
    default: *r=0;*g=0;*b=0; return;
  }
}

static void set_pixel(uint8_t r, uint8_t g, uint8_t b) {
  if (s_strip) {
    s_strip->setPixelColor(0, s_strip->Color(r, g, b));
    s_strip->show();
  }
}

void led_begin(int gpio_pin, int num_pixels) {
  s_pin = gpio_pin;
  if (s_strip) delete s_strip;
  s_strip = new Adafruit_NeoPixel(num_pixels > 0 ? num_pixels : PIXELS, gpio_pin, NEO_GRB + NEO_KHZ800);
  s_strip->begin();
  s_strip->setBrightness(255);
  s_strip->clear();
  s_strip->show();
  s_phase_start_ms = millis();
  s_blink_count = 0;
  s_stopped_double_done = false;
}

void led_set_state(UiState s) { s_state = s; s_phase_start_ms = millis(); s_blink_count = 0; s_stopped_double_done = false; }
void led_set_fault(LedFault f) { s_fault = f; s_phase_start_ms = millis(); s_blink_count = 0; }
void led_set_warning(LedWarning w) { s_warning = w; }
void led_clear_fault() { s_fault = LedFault::NONE; }
void led_clear_warning() { s_warning = LedWarning::NONE; }

void led_set_custom1(uint8_t r, uint8_t g, uint8_t b) { set_pixel(r, g, b); }
void led_set_custom2(uint8_t r, uint8_t g, uint8_t b) { set_pixel(r, g, b); }

// ---- Pattern helpers (no delay; use now_ms and phase_start) ----

static void run_fault_pattern(uint32_t now_ms) {
  uint8_t n = (uint8_t)s_fault;
  if (n == 0) return;
  const uint32_t on_ms = 120, off_ms = 120, long_off_ms = 1200;
  uint32_t cycle = on_ms + off_ms;
  uint32_t full_cycle = n * cycle + long_off_ms;
  uint32_t pos = (now_ms - s_phase_start_ms) % full_cycle;
  if (pos < n * cycle) {
    uint32_t bl = pos % cycle;
    if (bl < on_ms) {
      uint8_t r,g,b; get_color_rgb(LedColor::RED, DEFAULT_BRIGHTNESS, &r,&g,&b);
      set_pixel(r,g,b);
    } else
      set_pixel(0,0,0);
  } else
    set_pixel(0,0,0);
}

static void run_warning_pattern(uint32_t now_ms, LedColor col) {
  switch (s_warning) {
    case LedWarning::RTC_INVALID: {
      uint32_t pos = (now_ms - s_phase_start_ms) % 2000;
      if (pos < 40) { uint8_t r,g,b; get_color_rgb(LedColor::YELLOW, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else set_pixel(0,0,0);
      return;
    }
    case LedWarning::RTC_FAULT: {
      uint32_t pos = (now_ms - s_phase_start_ms) % 1200;
      if (pos < 120) { uint8_t r,g,b; get_color_rgb(LedColor::YELLOW, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else if (pos >= 240 && pos < 360) { uint8_t r,g,b; get_color_rgb(LedColor::YELLOW, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else set_pixel(0,0,0);
      return;
    }
    case LedWarning::LOW_BATT: {
      uint32_t period = 2000;
      uint32_t pos = (now_ms - s_phase_start_ms) % period;
      float t = (float)pos / period * 6.283185307f;
      uint8_t br = (uint8_t)(DEFAULT_BRIGHTNESS * (0.3f + 0.7f * (0.5f + 0.5f * sinf(t))));
      if (br < 10) br = 10;
      uint8_t r,g,b; get_color_rgb(LedColor::ORANGE, br, &r,&g,&b); set_pixel(r,g,b);
      return;
    }
    case LedWarning::UNDERLOAD: {
      uint32_t pos = (now_ms - s_phase_start_ms) % 500;
      if (pos < 120) { uint8_t r,g,b; get_color_rgb(LedColor::BLUE, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else set_pixel(0,0,0);
      return;
    }
    case LedWarning::OVERLOAD: {
      uint32_t pos = (now_ms - s_phase_start_ms) % 240;
      if (pos < 120) { uint8_t r,g,b; get_color_rgb(LedColor::YELLOW, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else set_pixel(0,0,0);
      return;
    }
    case LedWarning::COMPRESSION: {
      uint32_t pos = (now_ms - s_phase_start_ms) % 500;
      if (pos < 120) { uint8_t r,g,b; get_color_rgb(LedColor::MAGENTA, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else set_pixel(0,0,0);
      return;
    }
    case LedWarning::IMU_WARN: {
      uint32_t pos = (now_ms - s_phase_start_ms) % 1560;
      if (pos < 120) { uint8_t r,g,b; get_color_rgb(LedColor::MAGENTA, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else if (pos >= 240 && pos < 360) { uint8_t r,g,b; get_color_rgb(LedColor::MAGENTA, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else set_pixel(0,0,0);
      return;
    }
    default: break;
  }
}

static void run_state_pattern(uint32_t now_ms) {
  switch (s_state) {
    case UiState::BOOT: {
      uint32_t period = 1200;
      uint32_t pos = (now_ms - s_phase_start_ms) % period;
      float t = (float)pos / period * 6.283185307f;
      uint8_t br = (uint8_t)(DEFAULT_BRIGHTNESS * (0.3f + 0.7f * (0.5f + 0.5f * sinf(t))));
      if (br < 10) br = 10;
      uint8_t r,g,b; get_color_rgb(LedColor::RED, br, &r,&g,&b); set_pixel(r,g,b);
      return;
    }
    case UiState::IDLE_READY: {
      uint8_t r,g,b; get_color_rgb(LedColor::RED, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b);
      return;
    }
    case UiState::LOGGING: {
      uint32_t pos = (now_ms - s_phase_start_ms) % 1000;
      if (pos < 80) { uint8_t r,g,b; get_color_rgb(LedColor::GREEN, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else set_pixel(0,0,0);
      return;
    }
    case UiState::STOPPED: {
      if (s_stopped_double_done) {
        uint8_t r,g,b; get_color_rgb(LedColor::RED, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b);
        return;
      }
      const uint32_t on = 80, gap = 120, long_off = 1200;
      uint32_t cycle = on + gap + on + long_off;
      uint32_t pos = (now_ms - s_phase_start_ms) % cycle;
      if (pos < on || (pos >= on + gap && pos < on + gap + on))
        { uint8_t r,g,b; get_color_rgb(LedColor::GREEN, DEFAULT_BRIGHTNESS, &r,&g,&b); set_pixel(r,g,b); }
      else
        set_pixel(0,0,0);
      if ((now_ms - s_phase_start_ms) >= cycle)
        s_stopped_double_done = true;
      return;
    }
    case UiState::EXPORTING: {
      uint32_t period = 1000;
      uint32_t pos = (now_ms - s_phase_start_ms) % period;
      float t = (float)pos / period * 6.283185307f;
      uint8_t br = (uint8_t)(DEFAULT_BRIGHTNESS * (0.3f + 0.7f * (0.5f + 0.5f * sinf(t))));
      if (br < 10) br = 10;
      uint8_t r,g,b; get_color_rgb(LedColor::PURPLE, br, &r,&g,&b); set_pixel(r,g,b);
      return;
    }
    case UiState::FAULT:
      run_fault_pattern(now_ms);
      return;
  }
}

void led_tick(uint32_t now_ms) {
  if (!s_strip) return;

  if (s_fault != LedFault::NONE) {
    run_fault_pattern(now_ms);
    return;
  }
  if (s_warning != LedWarning::NONE && s_state == UiState::LOGGING) {
    run_warning_pattern(now_ms, LedColor::YELLOW);
    return;
  }
  if (s_warning != LedWarning::NONE && s_state != UiState::LOGGING) {
    run_warning_pattern(now_ms, LedColor::YELLOW);
    return;
  }
  run_state_pattern(now_ms);
}
