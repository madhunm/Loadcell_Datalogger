#pragma once
#include <Arduino.h>
#include "i2c_dev.h"

class MAX17048 {
public:
  static constexpr uint8_t ADDR_7BIT = 0x36;

  explicit MAX17048(TwoWire& w = Wire) : dev(w, ADDR_7BIT) {}

  bool begin() {
    // Basic presence check: read VERSION (0x08)
    uint16_t ver = 0;
    return dev.readU16BE(0x08, ver);
  }

  // Battery voltage in volts
  bool readVoltage(float& volts) {
    uint16_t raw;
    if (!dev.readU16BE(0x02, raw)) return false;
    // VCELL LSB = 78.125uV (per datasheet register summary)
    volts = float(raw) * 78.125e-6f;
    return true;
  }

  // SOC in percent (0..100+)
  bool readSoc(float& pct) {
    uint16_t raw;
    if (!dev.readU16BE(0x04, raw)) return false;
    // SOC LSB = 1%/256
    pct = float(raw) / 256.0f;
    return true;
  }

  // Optional: quick-start (MODE.QuickStart = 1). Datasheet shows QuickStart bit in MODE register. :contentReference[oaicite:5]{index=5}
  bool quickStart() {
    // MODE register bit layout is shown MSB->LSB as: X QuickStart EnSleep HibStat ...
    // => QuickStart is bit14 => 0x4000
    return dev.writeU16BE(0x06, 0x4000);
  }

  // Full POR reset: write 0x5400 to CMD (0xFE). Datasheet warns it may not ACK afterward. :contentReference[oaicite:6]{index=6}
  void resetPOR_noAckOK() {
    dev.wire.beginTransmission(dev.addr);
    dev.wire.write((uint8_t)0xFE);
    dev.wire.write((uint8_t)0x54);
    dev.wire.write((uint8_t)0x00);
    (void)dev.wire.endTransmission(); // ignore result by design
  }

private:
  I2CDev dev;
};
