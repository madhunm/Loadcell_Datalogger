#pragma once
#include <Arduino.h>
#include "i2c_dev.h"

struct RtcDateTime {
  uint16_t year;   // e.g. 2026
  uint8_t  month;  // 1..12
  uint8_t  day;    // 1..31
  uint8_t  hour;   // 0..23
  uint8_t  minute; // 0..59
  uint8_t  second; // 0..59
};

class RX8900CE {
public:
  static constexpr uint8_t ADDR_7BIT = 0x32;

  explicit RX8900CE(TwoWire& w = Wire) : dev(w, ADDR_7BIT) {}

  bool begin() { return dev.ping(); }

  bool readTime(RtcDateTime& t) {
    // Basic time/calendar registers start at 0x00
    uint8_t buf[7] = {};
    if (!dev.readBytes(0x00, buf, sizeof(buf))) return false;

    t.second = bcdToDec(buf[0] & 0x7F);
    t.minute = bcdToDec(buf[1] & 0x7F);
    t.hour   = bcdToDec(buf[2] & 0x3F);
    // buf[3] = WEEK (bitfield) - optional
    t.day    = bcdToDec(buf[4] & 0x3F);
    t.month  = bcdToDec(buf[5] & 0x1F);
    t.year   = 2000 + bcdToDec(buf[6]); // RX8900 is specified for 2000..2099
    return true;
  }

  bool writeTime(const RtcDateTime& t) {
    uint8_t buf[7] = {};
    buf[0] = decToBcd(t.second);
    buf[1] = decToBcd(t.minute);
    buf[2] = decToBcd(t.hour);
    buf[3] = 0x01;                 // WEEK: set “Day 1” by default (you can refine)
    buf[4] = decToBcd(t.day);
    buf[5] = decToBcd(t.month);
    buf[6] = decToBcd(uint8_t(t.year - 2000));
    return dev.writeBytes(0x00, buf, sizeof(buf));
  }

private:
  static uint8_t bcdToDec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
  static uint8_t decToBcd(uint8_t d) { return uint8_t(((d / 10) << 4) | (d % 10)); }

  I2CDev dev;
};
