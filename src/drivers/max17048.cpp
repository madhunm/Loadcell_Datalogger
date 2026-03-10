#include "max17048.h"

bool MAX17048::begin(TwoWire& wire) {
  _wire = &wire;
  uint16_t v = 0;
  return readWordBE(REG_VERSION, v);
}

bool MAX17048::readVoltage_mV(uint16_t& mv) {
  uint16_t raw = 0;
  if (!readWordBE(REG_VCELL, raw)) return false;

  // VCELL LSb = 78.125 µV => 0.078125 mV
  // mV = raw * 0.078125 = raw * 78125 / 1,000,000
  uint32_t num = (uint32_t)raw * 78125u;
  mv = (uint16_t)((num + 500000u) / 1000000u); // rounded
  return true;
}

bool MAX17048::readSOC_centiPercent(uint16_t& soc_centi) {
  uint16_t raw = 0;
  if (!readWordBE(REG_SOC, raw)) return false;

  // SOC LSb = 1/256 % => % = raw / 256
  // centi-% = raw * 100 / 256
  uint32_t num = (uint32_t)raw * 100u;
  soc_centi = (uint16_t)((num + 128u) / 256u); // rounded
  return true;
}

bool MAX17048::readCRate_centiPercentPerHour(int16_t& crate_centi) {
  uint16_t raw_u = 0;
  if (!readWordBE(REG_CRATE, raw_u)) return false;
  int16_t raw = (int16_t)raw_u;

  // CRATE LSb = 0.208 %/hr (datasheet)
  // centi-%/hr = raw * 0.208 * 100 = raw * 20.8
  // => raw * 208 / 10
  int32_t num = (int32_t)raw * 208;
  crate_centi = (int16_t)((num >= 0) ? ((num + 5) / 10) : ((num - 5) / 10));
  return true;
}

bool MAX17048::quickStart() {
  uint16_t mode = 0;
  if (!readWordBE(REG_MODE, mode)) return false;
  mode |= MODE_QUICKSTART;
  return writeWordBE(REG_MODE, mode);
}

bool MAX17048::readVersion(uint16_t& version) {
  return readWordBE(REG_VERSION, version);
}

bool MAX17048::readWordBE(uint8_t reg, uint16_t& out) {
  if (!_wire) return false;

  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;

  const uint8_t n = _wire->requestFrom((int)_addr, 2, (int)true);
  if (n != 2) return false;

  uint8_t msb = _wire->read();
  uint8_t lsb = _wire->read();
  out = (uint16_t)((msb << 8) | lsb);
  return true;
}

bool MAX17048::writeWordBE(uint8_t reg, uint16_t value) {
  if (!_wire) return false;

  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write((uint8_t)(value >> 8));   // MSB
  _wire->write((uint8_t)(value & 0xFF)); // LSB
  return (_wire->endTransmission(true) == 0);
}
