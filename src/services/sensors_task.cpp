#include <Arduino.h>
#include <Wire.h>

#include "services/sensors_task.h"
#include "services/aux_state.h"

// include your drivers
#include "drivers/max17048.h"
#include "drivers/rx8900ce.h"
#include "drivers/lsm6dsv320x.h"

static constexpr int I2C_SDA = 41;
static constexpr int I2C_SCL = 42;

static MAX17048 fuel(Wire);
static RX8900CE  rtc(Wire);
static LSM6DSV320X imu(Wire);

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

  Wire.begin(I2C_SDA, I2C_SCL, 400000);

  // Bring-up devices (don’t hard-fail the whole system on one missing device)
  bool fuel_ok = fuel.begin();
  bool rtc_ok  = rtc.begin();
  bool imu_ok  = imu.begin(/*accelOdrBits*/0x09, /*gyroOdrBits*/0x09); // ~960Hz ODR, read at 500Hz

  (void)fuel_ok;

  uint32_t last_batt_ms = 0;
  uint32_t last_rtc_ms  = 0;

  const TickType_t period = pdMS_TO_TICKS(2); // 500Hz
  TickType_t last_wake = xTaskGetTickCount();

  while (true) {
    vTaskDelayUntil(&last_wake, period);

    // IMU at 500Hz (reads latest registers; ODR ~960Hz keeps it fresh)
    if (imu_ok) {
      ImuSample s;
      if (imu.readSample(s)) {
        aux_set_imu(s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
      } else {
        aux_bump_i2c_err();
      }
    }

    uint32_t now = millis();

    // Fuel gauge at 1Hz (cache into frames)
    if (fuel_ok && (now - last_batt_ms) >= 1000) {
      last_batt_ms = now;
      float v = 0, soc = 0;
      if (fuel.readVoltage(v) && fuel.readSoc(soc)) {
        uint16_t mv = (uint16_t)lroundf(v * 1000.0f);
        uint16_t centi = (uint16_t)lroundf(soc * 100.0f); // % *100
        aux_set_batt(mv, centi);
      } else {
        aux_bump_i2c_err();
      }
    }

    // RTC at 2Hz (validity + epoch cache)
    if (rtc_ok && (now - last_rtc_ms) >= 500) {
      last_rtc_ms = now;
      RtcDateTime t;
      if (rtc.readTime(t)) {
        uint32_t epoch = toEpoch2000To2099(t.year, t.month, t.day, t.hour, t.minute, t.second);
        aux_set_rtc(epoch, true);
      } else {
        aux_set_rtc(0, false);
        aux_bump_i2c_err();
      }
    }
  }
}

void start_sensors_task() {
  xTaskCreatePinnedToCore(sensors_task, "sensors", 4096, nullptr, 3, nullptr, 0);
}
