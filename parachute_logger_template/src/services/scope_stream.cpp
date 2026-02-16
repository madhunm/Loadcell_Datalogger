#include "services/scope_stream.h"
#include <cmath>
#include <limits>

QueueHandle_t g_scope_q = nullptr;

static bool g_enabled = false;
static uint16_t g_N = 20; // default for 25Hz
static float g_accel_g_per_lsb = 1.0f / 16384.0f;

struct Agg {
  uint16_t n=0;
  int64_t sum_mean_mN=0;
  int32_t max_peak_mN=INT32_MIN;
  float max_acc_g=0.0f;
  uint16_t flags_or=0;
  uint32_t last_ms=0;
  void reset() {
    n=0; sum_mean_mN=0; max_peak_mN=INT32_MIN; max_acc_g=0.0f; flags_or=0; last_ms=0;
  }
} a;

void scope_init() { g_scope_q = xQueueCreate(1, sizeof(ScopeSample)); a.reset(); }
void scope_set_accel_scale(float s) { g_accel_g_per_lsb = s; }

void scope_set_rate(uint16_t hz) {
  if (hz == 0) { g_enabled = false; a.reset(); return; }
  if ((500 % hz) != 0) return;
  g_N = 500 / hz;
  a.reset();
  g_enabled = true;
}

bool scope_is_enabled() { return g_enabled; }

void scope_feed_frame(const PdlFrameV1& fr) {
  if (!g_enabled) return;

  a.sum_mean_mN += fr.force_mean_mN;
  if (fr.force_peak_mN > a.max_peak_mN) a.max_peak_mN = fr.force_peak_mN;

  float ax = fr.ax * g_accel_g_per_lsb;
  float ay = fr.ay * g_accel_g_per_lsb;
  float az = fr.az * g_accel_g_per_lsb;
  float mag = sqrtf(ax*ax + ay*ay + az*az);
  if (mag > a.max_acc_g) a.max_acc_g = mag;

  a.flags_or |= fr.flags;
  a.last_ms = (uint32_t)(fr.t_us / 1000ULL);

  a.n++;
  if (a.n >= g_N) {
    ScopeSample s;
    s.ms = a.last_ms;
    s.force_mean_N = (float)((a.sum_mean_mN / (double)g_N) / 1000.0);
    s.force_peak_N = (float)(a.max_peak_mN / 1000.0);
    s.accel_mag_g = a.max_acc_g;
    s.flags = a.flags_or;

    a.reset();
    if (g_scope_q) xQueueOverwrite(g_scope_q, &s);
  }
}
