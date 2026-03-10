#include <Arduino.h>
#include <Wire.h>
#include "esp_timer.h"

#include "services/sensors_task.h"
#include "services/aux_state.h"
#include "services/scope_stream.h"
#include "services/system_status.h"
#include "drivers/max17048.h"
#include "drivers/rx8900ce.h"
#include "drivers/lsm6dsv.h"
#include "pins.h"

static MAX17048 fuel;
static RX8900CE rtc;
static LSM6DSV imu;

static volatile uint32_t s_imu_irq_count = 0;
static volatile bool s_rtc_pending = false;
static volatile bool s_retry_requested = false;

static void IRAM_ATTR imu_int1_isr() {
  uint32_t c = s_imu_irq_count;
  s_imu_irq_count = c + 1;
}

static void IRAM_ATTR rtc_int_isr() {
  s_rtc_pending = true;
}

static bool isLeap(int y) { return (y % 4) == 0; } // valid for 2000..2099 usage

static uint32_t toEpoch2000To2099(uint16_t year, uint8_t mon, uint8_t day,
                                  uint8_t hour, uint8_t min, uint8_t sec) {
  // Convert calendar to epoch seconds (1970-based) with a simple days count.
  // Good enough for 2000..2099 (your RTC range).
  static const uint16_t mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

  // days from 1970-01-01 to year-01-01
  int32_t days = 0;
  for (int y = 1970; y < (int)year; y++) days += isLeap(y) ? 366 : 365;

  // days in current year before mon/day
  uint16_t mdays[12];
  for (int i=0;i<12;i++) mdays[i]=mdays_norm[i];
  if (isLeap(year)) mdays[1]=29;

  for (int m=1; m < (int)mon; m++) days += mdays[m-1];
  days += (int)day - 1;

  return (uint32_t)days * 86400u + (uint32_t)hour * 3600u + (uint32_t)min * 60u + (uint32_t)sec;
}

static void sensors_task(void*) {
  aux_init();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  // Bring-up devices (don't hard-fail the whole system on one missing device)
  bool fuel_ok = fuel.begin(Wire);
  static bool rtc_ok = rtc.begin(Wire);
  static bool imu_ok = imu.begin(Wire) && imu.configure(LSM6DSV::Odr::HZ_960, LSM6DSV::Odr::HZ_960);

  if (imu_ok) {
    aux_set_imu_scales(imu.accel_g_per_lsb(), imu.gyro_dps_per_lsb());
    aux_set_imu_valid(true);
    scope_set_accel_scale(0.000488f);
  } else {
    aux_set_imu_valid(false);
  }
  if (!imu_ok) system_status_set_fault(FaultCode::IMU_FAULT);
  if (!fuel_ok) aux_set_batt(0, 0);

  if (imu_ok) {
    imu.setIntPinConfig(false, false);
    imu.routeDrdyToInt1(true, true);
    pinMode(PIN_IMU_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT1), imu_int1_isr, RISING);
  }
  if (!rtc_ok) {
    aux_set_rtc(0, false);
    system_status_set_warning(WarningCode::RTC_FAULT);
  }
  if (rtc_ok) {
    rtc.enableSecondUpdateInterrupt(true);
    pinMode(PIN_RTC_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_RTC_INT), rtc_int_isr, FALLING);
    // One immediate read so a log started right after boot gets valid RTC filename/header.
    uint8_t flags = 0;
    if (rtc.readFlags(flags) && !(flags & RX8900CE::FLAG_VLF)) {
      RX8900CE::DateTime t;
      if (rtc.readDateTime(t)) {
        uint32_t epoch = toEpoch2000To2099(t.year, t.month, t.day, t.hour, t.minute, t.second);
        aux_set_rtc(epoch, true);
        system_status_clear_warning(WarningCode::RTC_FAULT);
      } else {
        aux_set_rtc(0, false);
        system_status_set_warning(WarningCode::RTC_FAULT);
      }
    } else {
      aux_set_rtc(0, false);
      system_status_set_warning(WarningCode::RTC_FAULT);
    }
  }

  uint32_t last_batt_ms = millis() - 1000;
  uint32_t last_imu_count = 0;

  const TickType_t period = pdMS_TO_TICKS(2);
  TickType_t last_wake = xTaskGetTickCount();

  while (true) {
    vTaskDelayUntil(&last_wake, period);

    if (s_retry_requested) {
      s_retry_requested = false;
      if (!fuel_ok) fuel_ok = fuel.begin(Wire);
      imu_ok = imu.begin(Wire) && imu.configure(LSM6DSV::Odr::HZ_960, LSM6DSV::Odr::HZ_960);
      rtc_ok = rtc.begin(Wire);
      if (imu_ok) {
        system_status_clear_fault(FaultCode::IMU_FAULT);
        aux_set_imu_scales(imu.accel_g_per_lsb(), imu.gyro_dps_per_lsb());
        aux_set_imu_valid(true);
        scope_set_accel_scale(0.000488f);
        imu.setIntPinConfig(false, false);
        imu.routeDrdyToInt1(true, true);
        pinMode(PIN_IMU_INT1, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT1), imu_int1_isr, RISING);
      } else {
        aux_set_imu_valid(false);
      }
      if (rtc_ok) {
        system_status_clear_warning(WarningCode::RTC_FAULT);
        rtc.enableSecondUpdateInterrupt(true);
        pinMode(PIN_RTC_INT, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PIN_RTC_INT), rtc_int_isr, FALLING);
        uint8_t flags = 0;
        if (rtc.readFlags(flags) && !(flags & RX8900CE::FLAG_VLF)) {
          RX8900CE::DateTime t;
          if (rtc.readDateTime(t)) {
            uint32_t epoch = toEpoch2000To2099(t.year, t.month, t.day, t.hour, t.minute, t.second);
            aux_set_rtc(epoch, true);
          } else {
            aux_set_rtc(0, false);
            system_status_set_warning(WarningCode::RTC_FAULT);
          }
        } else {
          aux_set_rtc(0, false);
          system_status_set_warning(WarningCode::RTC_FAULT);
        }
      }
      if (!rtc_ok) {
        aux_set_rtc(0, false);
        system_status_set_warning(WarningCode::RTC_FAULT);
      }
      if (!fuel_ok) aux_set_batt(0, 0);
      last_batt_ms = millis() - 1000;
    }

    uint32_t now = millis();

    if (imu_ok && s_imu_irq_count != last_imu_count) {
      last_imu_count = s_imu_irq_count;
      LSM6DSV::SampleRaw s;
      if (imu.readRaw(s)) {
        uint64_t t_us = (uint64_t)esp_timer_get_time();
        aux_set_imu(s.ax, s.ay, s.az, s.gx, s.gy, s.gz, t_us);
        system_status_clear_warning(WarningCode::IMU_WARN);
      } else {
        aux_bump_i2c_err();
        aux_set_imu_valid(false);
        system_status_set_warning(WarningCode::IMU_WARN);
      }
    }

    // RTC provides session start time and filename stamping only; per-frame timestamps are monotonic t_us.
    if (s_rtc_pending && rtc_ok) {
      uint8_t flags = 0;
      if (!rtc.readFlags(flags)) {
        aux_bump_i2c_err();
        system_status_set_warning(WarningCode::RTC_FAULT);
        aux_set_rtc(0, false);
        s_rtc_pending = false;
        rtc.clearFlags(RX8900CE::FLAG_UF);
      } else if (flags & RX8900CE::FLAG_VLF) {
        aux_set_rtc(0, false);
        system_status_set_warning(WarningCode::RTC_FAULT);
        s_rtc_pending = false;
        rtc.clearFlags(RX8900CE::FLAG_UF);
      } else {
        RX8900CE::DateTime t;
        if (rtc.readDateTime(t)) {
          uint32_t epoch = toEpoch2000To2099(t.year, t.month, t.day, t.hour, t.minute, t.second);
          aux_set_rtc(epoch, true);
          system_status_clear_warning(WarningCode::RTC_FAULT);
        } else {
          aux_set_rtc(0, false);
          aux_bump_i2c_err();
          system_status_set_warning(WarningCode::RTC_FAULT);
        }
        s_rtc_pending = false;
        rtc.clearFlags(RX8900CE::FLAG_UF);
      }
    }

    if (fuel_ok && (now - last_batt_ms) >= 1000) {
      last_batt_ms = now;
      uint16_t mv = 0, centi = 0;
      if (fuel.readVoltage_mV(mv) && fuel.readSOC_centiPercent(centi)) {
        aux_set_batt(mv, centi);
      } else {
        aux_bump_i2c_err();
        aux_set_batt(0, 0);
        system_status_set_warning(WarningCode::LOW_BATT);
      }
    }

  }
}

void start_sensors_task() {
  xTaskCreatePinnedToCore(sensors_task, "sensors", 4096, nullptr, 3, nullptr, 0);
}

void sensors_request_retry_probe() {
  s_retry_requested = true;
}
