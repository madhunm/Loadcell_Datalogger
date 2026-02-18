#pragma once
#include <Arduino.h>
#include <Wire.h>

class RX8900CE {
public:
  struct DateTime {
    uint16_t year;   // 2000..2099 (driver assumes 2000+ for 00..99)
    uint8_t month;   // 1..12
    uint8_t day;     // 1..31
    uint8_t hour;    // 0..23
    uint8_t minute;  // 0..59
    uint8_t second;  // 0..59
    uint8_t weekday; // 0..6 (bit position used in WEEK reg)
  };

  explicit RX8900CE(uint8_t i2c_addr = 0x32) : _addr(i2c_addr) {}

  bool begin(TwoWire& wire = Wire);

  bool readDateTime(DateTime& dt);
  bool setDateTime(const DateTime& dt);

  bool readFlags(uint8_t& flags);
  bool clearFlags(uint8_t mask_to_clear);

  bool readControl(uint8_t& ctrl);
  bool writeControl(uint8_t ctrl);

  // Sets RESET bit in control register; executes on I2C STOP and auto-clears.
  bool pulseReset();

  // Convenience: UNIX time (seconds since 1970-01-01 00:00:00 UTC).
  bool readUnix(uint32_t& epoch);

  bool enableSecondUpdateInterrupt(bool enable);
  bool setFoutFrequency(uint8_t hz1);

  static uint32_t toUnix(const DateTime& dt);

  static constexpr uint8_t FLAG_UF   = (1u << 5);
  static constexpr uint8_t FLAG_TF   = (1u << 4);
  static constexpr uint8_t FLAG_AF   = (1u << 3);
  static constexpr uint8_t FLAG_VLF  = (1u << 1);
  static constexpr uint8_t FLAG_VDET = (1u << 0);

private:
  TwoWire* _wire = nullptr;
  uint8_t _addr;

  static constexpr uint8_t REG_SEC     = 0x00;
  static constexpr uint8_t REG_MIN     = 0x01;
  static constexpr uint8_t REG_HOUR    = 0x02;
  static constexpr uint8_t REG_WEEK    = 0x03;
  static constexpr uint8_t REG_DAY     = 0x04;
  static constexpr uint8_t REG_MONTH   = 0x05;
  static constexpr uint8_t REG_YEAR    = 0x06;

  static constexpr uint8_t REG_EXT     = 0x0D;
  static constexpr uint8_t REG_FLAG    = 0x0E;
  static constexpr uint8_t REG_CTRL    = 0x0F;
  static constexpr uint8_t REG_RTCID   = 0x30;  // model ID, expect 0xC4

  static constexpr uint8_t CTRL_UIE   = (1u << 5);
  static constexpr uint8_t CTRL_TIE   = (1u << 4);
  static constexpr uint8_t CTRL_AIE   = (1u << 3);
  static constexpr uint8_t CTRL_RESET = (1u << 0);

  static uint8_t bcdToBin(uint8_t bcd);
  static uint8_t binToBcd(uint8_t bin);

  bool readReg(uint8_t reg, uint8_t& val);
  bool writeReg(uint8_t reg, uint8_t val);
  bool readBurst(uint8_t start_reg, uint8_t* buf, size_t len);
  bool writeBurst(uint8_t start_reg, const uint8_t* buf, size_t len);

  static int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d);
};
