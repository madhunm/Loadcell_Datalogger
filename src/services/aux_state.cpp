#include "services/aux_state.h"
#include "freertos/portmacro.h"

static AuxSnapshot g_aux;
static portMUX_TYPE g_aux_mux = portMUX_INITIALIZER_UNLOCKED;

void aux_init() {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux = AuxSnapshot{};
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_imu(int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz, uint64_t imu_sample_t_us) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.ax=ax; g_aux.ay=ay; g_aux.az=az;
  g_aux.gx=gx; g_aux.gy=gy; g_aux.gz=gz;
  g_aux.imu_valid = true;
  g_aux.imu_sample_t_us = imu_sample_t_us;
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_imu_valid(bool valid) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.imu_valid = valid;
  if (!valid) {
    g_aux.ax = 0; g_aux.ay = 0; g_aux.az = 0;
    g_aux.gx = 0; g_aux.gy = 0; g_aux.gz = 0;
    g_aux.imu_sample_t_us = 0;
  }
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_batt(uint16_t vbat_mV, uint16_t soc_centiPct) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.vbat_mV = vbat_mV;
  g_aux.soc_centiPct = soc_centiPct;
  g_aux.battery_valid = true;
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_batt_invalid() {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.vbat_mV = 0;
  g_aux.soc_centiPct = 0;
  g_aux.battery_valid = false;
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_rtc(uint32_t epoch, bool valid) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.rtc_epoch = epoch;
  g_aux.rtc_valid = valid;
  portEXIT_CRITICAL(&g_aux_mux);
}

void aux_set_imu_scales(float accel_g_per_lsb, float gyro_dps_per_lsb) {
  portENTER_CRITICAL(&g_aux_mux);
  g_aux.accel_g_per_lsb = accel_g_per_lsb;
  g_aux.gyro_dps_per_lsb = gyro_dps_per_lsb;
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
