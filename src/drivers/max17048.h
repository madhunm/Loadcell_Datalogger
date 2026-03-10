#pragma once
#include <Arduino.h>
#include <Wire.h>

class MAX17048 {
public:
  explicit MAX17048(uint8_t i2c_addr = 0x36) : _addr(i2c_addr) {}

  bool begin(TwoWire& wire = Wire);

  // Reads battery voltage in millivolts (rounded).
  bool readVoltage_mV(uint16_t& mv);

  // Reads SOC in centi-percent (e.g., 7543 = 75.43%).
  bool readSOC_centiPercent(uint16_t& soc_centi);

  // Optional: charge/discharge rate in centi-%/hr (signed).
  bool readCRate_centiPercentPerHour(int16_t& crate_centi);

  // Triggers a Quick-Start via MODE register (use cautiously).
  bool quickStart();

  bool readVersion(uint16_t& version);

private:
  TwoWire* _wire = nullptr;
  uint8_t _addr;

  static constexpr uint8_t REG_VCELL   = 0x02;
  static constexpr uint8_t REG_SOC     = 0x04;
  static constexpr uint8_t REG_MODE    = 0x06;
  static constexpr uint8_t REG_VERSION = 0x08;
  static constexpr uint8_t REG_CRATE   = 0x16;

  // MODE bits (from datasheet register format): bit14 QuickStart, bit13 EnSleep, bit12 HibStat(ro)
  static constexpr uint16_t MODE_QUICKSTART = (1u << 14);

  bool readWordBE(uint8_t reg, uint16_t& out);   // MSB @ reg, LSB @ reg+1
  bool writeWordBE(uint8_t reg, uint16_t value); // MSB first
};
