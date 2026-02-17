#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace i2c_util {

inline bool writeReg8(TwoWire& w, uint8_t addr, uint8_t reg, uint8_t v) {
  w.beginTransmission(addr);
  w.write(reg);
  w.write(v);
  return (w.endTransmission(true) == 0);
}

inline bool readBytes(TwoWire& w, uint8_t addr, uint8_t reg, uint8_t* out, size_t n) {
  w.beginTransmission(addr);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false; // repeated-start
  size_t got = w.requestFrom((int)addr, (int)n, (int)true);
  if (got != n) return false;
  for (size_t i = 0; i < n; i++) out[i] = (uint8_t)w.read();
  return true;
}

inline bool readReg8(TwoWire& w, uint8_t addr, uint8_t reg, uint8_t* out) {
  return readBytes(w, addr, reg, out, 1);
}

} // namespace i2c_util
