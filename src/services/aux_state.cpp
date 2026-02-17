#include "services/aux_state.h"
#include "freertos/portmacro.h"

static AuxSnapshot g_aux;
static portMUX_TYPE g_aux_mux = portMUX_INITIALIZER_UNLOCKED;

void aux_init() {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux = AuxSnapshot{};
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_imu(int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.ax=ax; g_aux.ay=ay; g_aux.az=az;
  g_aux.gx=gx; g_aux.gy=gy; g_aux.gz=gz;
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_batt(uint16_t vbat_mV, uint16_t soc_centiPct) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.vbat_mV = vbat_mV;
  g_aux.soc_centiPct = soc_centiPct;
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_rtc(uint32_t epoch, bool valid) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.rtc_epoch = epoch;
  g_aux.rtc_valid = valid;
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_bump_i2c_err() {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.i2c_err_count++;
  portEXIT_CRITICAL(&g_aux_mux);
}

AuxSnapshot aux_get_snapshot() {
  AuxSnapshot s;
  portENTER_CRITICAL(&g_aux_mux);
  s = g_aux;
  portEXIT_CRITICAL(&g_aux_mux);
  return s;
}
