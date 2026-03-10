#pragma once
#include <cstdint>
#include "freertos/FreeRTOS.h"

struct AuxSnapshot {
  // IMU raw
  int16_t ax=0, ay=0, az=0;
  int16_t gx=0, gy=0, gz=0;

  // IMU sample timestamp (esp_timer_get_time() when sample was read)
  uint64_t imu_sample_t_us=0;

  // IMU scale factors (0 = use header defaults)
  float accel_g_per_lsb=0.f;
  float gyro_dps_per_lsb=0.f;

  // Battery
  uint16_t vbat_mV=0;
  uint16_t soc_centiPct=0; // 10000 = 100.00%

  // RTC (optional)
  uint32_t rtc_epoch=0;    // seconds since 1970, if valid
  bool rtc_valid=false;

  // Diagnostics
  uint32_t i2c_err_count=0;
};

void aux_init();
void aux_set_imu(int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz, uint64_t imu_sample_t_us = 0);
void aux_set_batt(uint16_t vbat_mV, uint16_t soc_centiPct);
void aux_set_rtc(uint32_t epoch, bool valid);
void aux_set_imu_scales(float accel_g_per_lsb, float gyro_dps_per_lsb);
void aux_bump_i2c_err();
AuxSnapshot aux_get_snapshot();
