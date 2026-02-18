#pragma once
#include <cstdint>
#include <Arduino.h>              
#include "format/log_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

struct ScopeSample {
  uint32_t ms;
  float force_mean_N;
  float force_peak_N;
  float accel_mag_g;
  uint16_t flags;
};

void scope_init();
void scope_set_rate(uint16_t hz); // 0 stops; hz must divide 500
bool scope_is_enabled();

void scope_feed_frame(const PdlFrameV1& fr);

extern QueueHandle_t g_scope_q;

void scope_set_accel_scale(float accel_g_per_lsb);
