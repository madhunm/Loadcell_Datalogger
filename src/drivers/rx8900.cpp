#include "rx8900ce.h"

bool RX8900CE::begin(TwoWire& wire) {
  _wire = &wire;
  uint8_t f = 0;
  return readReg(REG_FLAG, f);
}

bool RX8900CE::readDateTime(DateTime& dt) {
  uint8_t b[7] = {0};
  if (!readBurst(REG_SEC, b, sizeof(b))) return false;

  // Mask off reserved bits where applicable
  uint8_t sec   = b[0] & 0x7F;
  uint8_t min   = b[1] & 0x7F;
  uint8_t hour  = b[2] & 0x3F;
  uint8_t week  = b[3] & 0x7F;
  uint8_t day   = b[4] & 0x3F;
  uint8_t month = b[5] & 0x1F;
  uint8_t year  = b[6]; // full BCD (00..99)

  dt.second = bcdToBin(sec);
  dt.minute = bcdToBin(min);
  dt.hour   = bcdToBin(hour);
  dt.day    = bcdToBin(day);
  dt.month  = bcdToBin(month);
  dt.year   = 2000u + bcdToBin(year);

  // WEEK is a bitfield. Pick the lowest set bit as weekday index.
  uint8_t wd = 0;
  if (week != 0) {
    while (wd < 7 && ((week & (1u << wd)) == 0)) wd++;
    if (wd >= 7) wd = 0;
  }
  dt.weekday = wd;
  return true;
}

bool RX8900CE::setDateTime(const DateTime& dt) {
  if (dt.year < 2000 || dt.year > 2099) return false;
  if (dt.month < 1 || dt.month > 12) return false;
  if (dt.day < 1 || dt.day > 31) return false;
  if (dt.hour > 23 || dt.minute > 59 || dt.second > 59) return false;
  if (dt.weekday > 6) return false;

  uint8_t b[7] = {0};
  b[0] = binToBcd(dt.second) & 0x7F;
  b[1] = binToBcd(dt.minute) & 0x7F;
  b[2] = binToBcd(dt.hour)   & 0x3F;
  b[3] = (uint8_t)(1u << dt.weekday);  // WEEK is bitfield
  b[4] = binToBcd(dt.day)    & 0x3F;
  b[5] = binToBcd(dt.month)  & 0x1F;
  b[6] = binToBcd((uint8_t)(dt.year - 2000));

  return writeBurst(REG_SEC, b, sizeof(b));
}

bool RX8900CE::readFlags(uint8_t& flags) {
  return readReg(REG_FLAG, flags);
}

bool RX8900CE::clearFlags(uint8_t mask_to_clear) {
  uint8_t f = 0;
  if (!readReg(REG_FLAG, f)) return false;
  // Only 0 clears; writing 1 is ignored on flag bits.
  f = (uint8_t)(f & ~mask_to_clear);
  return writeReg(REG_FLAG, f);
}

bool RX8900CE::readControl(uint8_t& ctrl) {
  return readReg(REG_CTRL, ctrl);
}

bool RX8900CE::writeControl(uint8_t ctrl) {
  // Reserved bits must stay 0 (bits 2:1 are "0" in manual)
  ctrl &= (uint8_t)(0b11111001);
  return writeReg(REG_CTRL, ctrl);
}

bool RX8900CE::pulseReset() {
  uint8_t c = 0;
  if (!readControl(c)) return false;
  c |= CTRL_RESET;
  return writeControl(c); // executes on STOP, auto-clears
}

bool RX8900CE::readUnix(uint32_t& epoch) {
  DateTime dt{};
  if (!readDateTime(dt)) return false;
  epoch = toUnix(dt);
  return true;
}

bool RX8900CE::enableSecondUpdateInterrupt(bool enable) {
  uint8_t ext = 0;
  if (!readReg(REG_EXT, ext)) return false;
  ext &= ~(1u << 7);
  if (!enable) ext |= (1u << 7);
  if (!writeReg(REG_EXT, ext)) return false;
  uint8_t ctrl = 0;
  if (!readControl(ctrl)) return false;
  if (enable) ctrl |= CTRL_UIE;
  else ctrl &= (uint8_t)~CTRL_UIE;
  return writeControl(ctrl);
}

bool RX8900CE::setFoutFrequency(uint8_t hz1) {
  uint8_t ext = 0;
  if (!readReg(REG_EXT, ext)) return false;
  ext &= 0x0F;
  if (hz1 == 1) ext |= (1u << 4);
  return writeReg(REG_EXT, ext);
}

uint8_t RX8900CE::bcdToBin(uint8_t bcd) {
  return (uint8_t)((bcd & 0x0F) + 10u * ((bcd >> 4) & 0x0F));
}

uint8_t RX8900CE::binToBcd(uint8_t bin) {
  return (uint8_t)(((bin / 10u) << 4) | (bin % 10u));
}

bool RX8900CE::readReg(uint8_t reg, uint8_t& val) {
  if (!_wire) return false;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;
  if (_wire->requestFrom((int)_addr, 1, (int)true) != 1) return false;
  val = _wire->read();
  return true;
}

bool RX8900CE::writeReg(uint8_t reg, uint8_t val) {
  if (!_wire) return false;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(val);
  return (_wire->endTransmission(true) == 0);
}

bool RX8900CE::readBurst(uint8_t start_reg, uint8_t* buf, size_t len) {
  if (!_wire || !buf || len == 0) return false;
  _wire->beginTransmission(_addr);
  _wire->write(start_reg);
  if (_wire->endTransmission(false) != 0) return false;

  size_t got = _wire->requestFrom((int)_addr, (int)len, (int)true);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = _wire->read();
  return true;
}

bool RX8900CE::writeBurst(uint8_t start_reg, const uint8_t* buf, size_t len) {
  if (!_wire || !buf || len == 0) return false;
  _wire->beginTransmission(_addr);
  _wire->write(start_reg);
  for (size_t i = 0; i < len; i++) _wire->write(buf[i]);
  return (_wire->endTransmission(true) == 0);
}

// Howard Hinnant's days-from-civil (Gregorian), returns days since 1970-01-01
int32_t RX8900CE::daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= (m <= 2);
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int32_t)(era * 146097 + (int32_t)doe - 719468); // 719468 = days to 1970-01-01
}

uint32_t RX8900CE::toUnix(const DateTime& dt) {
  int32_t days = daysFromCivil((int32_t)dt.year, dt.month, dt.day);
  int32_t secs = days * 86400 + (int32_t)dt.hour * 3600 + (int32_t)dt.minute * 60 + (int32_t)dt.second;
  if (secs < 0) return 0;
  return (uint32_t)secs;
}
