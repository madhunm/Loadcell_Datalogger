#include "drivers/rx8900.h"
#include "drivers/i2c_util.h"

namespace {
static constexpr uint8_t REG_SEC   = 0x00;
static constexpr uint8_t REG_MIN   = 0x01;
static constexpr uint8_t REG_HOUR  = 0x02;
static constexpr uint8_t REG_WEEK  = 0x03;
static constexpr uint8_t REG_DAY   = 0x04;
static constexpr uint8_t REG_MONTH = 0x05;
static constexpr uint8_t REG_YEAR  = 0x06;

static constexpr uint8_t REG_FLAG  = 0x0E;
static constexpr uint8_t REG_CTRL  = 0x0F;

static constexpr uint8_t REG_TEMP  = 0x17;
} // namespace

uint8_t RX8900::bcdToBin_(uint8_t bcd) {
  return (uint8_t)((bcd & 0x0F) + 10 * ((bcd >> 4) & 0x0F));
}
uint8_t RX8900::binToBcd_(uint8_t v) {
  return (uint8_t)(((v / 10) << 4) | (v % 10));
}

bool RX8900::begin(TwoWire& w, uint8_t i2c_addr_7bit) {
  w_ = &w;
  addr_ = i2c_addr_7bit;

  // Optional: read flags to ensure device acks
  uint8_t f = 0;
  return readFlags(&f);
}

bool RX8900::readTime(DateTime* out) {
  if (!w_ || !out) return false;
  uint8_t b[7];
  if (!i2c_util::readBytes(*w_, addr_, REG_SEC, b, sizeof(b))) return false;

  // All time/calendar fields are BCD (per register table)
  out->sec  = bcdToBin_(b[0] & 0x7F);
  out->min  = bcdToBin_(b[1] & 0x7F);
  out->hour = bcdToBin_(b[2] & 0x3F);
  out->week = (uint8_t)(b[3] & 0x07);
  out->day  = bcdToBin_(b[4] & 0x3F);
  out->month= bcdToBin_(b[5] & 0x1F);
  uint8_t yy= bcdToBin_(b[6]);
  out->year = (uint16_t)(2000 + yy);
  return true;
}

bool RX8900::setTime(const DateTime& dt) {
  if (!w_) return false;

  // Write starting at 0x00
  w_->beginTransmission(addr_);
  w_->write(REG_SEC);
  w_->write(binToBcd_(dt.sec));
  w_->write(binToBcd_(dt.min));
  w_->write(binToBcd_(dt.hour));
  w_->write(dt.week & 0x07);
  w_->write(binToBcd_(dt.day));
  w_->write(binToBcd_(dt.month));
  w_->write(binToBcd_((uint8_t)(dt.year >= 2000 ? (dt.year - 2000) : dt.year)));
  return (w_->endTransmission(true) == 0);
}

bool RX8900::readFlags(uint8_t* out_flag_reg) {
  if (!w_ || !out_flag_reg) return false;
  return i2c_util::readReg8(*w_, addr_, REG_FLAG, out_flag_reg);
}

bool RX8900::clearFlags(uint8_t mask) {
  if (!w_) return false;
  uint8_t f = 0;
  if (!readFlags(&f)) return false;
  f = (uint8_t)(f & ~mask);
  return i2c_util::writeReg8(*w_, addr_, REG_FLAG, f);
}

bool RX8900::readTemperatureC(float* out_c) {
  if (!w_ || !out_c) return false;
  uint8_t t = 0;
  if (!i2c_util::readReg8(*w_, addr_, REG_TEMP, &t)) return false;

  // Manual formula:
  // Temperature [°C] = (TEMP[7:0] * 2 – 187.19) / 3.218
  *out_c = ( (float)t * 2.0f - 187.19f ) / 3.218f;
  return true;
}
