#include "lsm6dsv.h"

bool LSM6DSV::begin(TwoWire& wire, uint8_t addr) {
  _wire = &wire;

  // Try addr then alternate (0x6A/0x6B)
  uint8_t who = 0;
  _addr = addr;
  if (readWhoAmI(who) && who == WHOAMI_EXPECTED) return true;

  _addr = (addr == 0x6A) ? 0x6B : 0x6A;
  if (readWhoAmI(who) && who == WHOAMI_EXPECTED) return true;

  return false;
}

bool LSM6DSV::readWhoAmI(uint8_t& whoami) {
  return readReg(REG_WHO_AM_I, whoami);
}

bool LSM6DSV::softReset() {
  // CTRL3: BOOT | BDU | 0 | 0 | 0 | IF_INC | 0 | SW_RESET
  // We set SW_RESET=1; it auto-clears. Keep BDU=1, IF_INC=1.
  uint8_t ctrl3 = 0;
  if (!readReg(REG_CTRL3, ctrl3)) return false;

  ctrl3 |= (1u << 0); // SW_RESET
  // Ensure reserved bits that "must be 0" remain 0; keep BDU/IF_INC as-is.
  ctrl3 &= 0b11000101;
  ctrl3 |= (1u << 6); // BDU=1 (recommended)
  ctrl3 |= (1u << 2); // IF_INC=1 (recommended)

  if (!writeReg(REG_CTRL3, ctrl3)) return false;

  // Wait for reset to clear (simple bounded poll)
  for (uint8_t i = 0; i < 50; i++) {
    uint8_t v = 0;
    if (!readReg(REG_CTRL3, v)) return false;
    if ((v & 0x01) == 0) return true;
    delay(2);
  }
  return false;
}

bool LSM6DSV::configure(Odr odr_xl, Odr odr_g, FsXl fs_xl, FsG fs_g,
                       bool enable_gyro_lpf1, uint8_t gyro_lpf1_bw) {
  if (!_wire) return false;

  _odr_xl = odr_xl;
  _odr_g  = odr_g;
  _fs_xl  = fs_xl;
  _fs_g   = fs_g;

  if (!softReset()) return false;

  // CTRL1: bit7 must be 0, OP_MODE_XL[2:0]=000 (high-performance), ODR_XL[3:0]
  uint8_t ctrl1 = (uint8_t)((0u << 4) | ((uint8_t)odr_xl & 0x0F));
  ctrl1 &= 0x7F;
  if (!writeReg(REG_CTRL1, ctrl1)) return false;

  // CTRL2: bit7 must be 0, OP_MODE_G[2:0]=000 (high-performance), ODR_G[3:0]
  uint8_t ctrl2 = (uint8_t)((0u << 4) | ((uint8_t)odr_g & 0x0F));
  ctrl2 &= 0x7F;
  if (!writeReg(REG_CTRL2, ctrl2)) return false;

  // CTRL6: bit7 must be 0, LPF1_G_BW[2:0] in bits6..4, FS_G[3:0] in bits3..0
  gyro_lpf1_bw &= 0x07;
  uint8_t ctrl6 = (uint8_t)((gyro_lpf1_bw << 4) | ((uint8_t)fs_g & 0x0F));
  ctrl6 &= 0x7F;
  if (!writeReg(REG_CTRL6, ctrl6)) return false;

  // CTRL7: LPF1_G_EN bit0, others must be 0
  uint8_t ctrl7 = enable_gyro_lpf1 ? 0x01 : 0x00;
  if (!writeReg(REG_CTRL7, ctrl7)) return false;

  // CTRL8: HP_LPF2_XL_BW[2:0] bits7..5 (we leave 0), bit4 must be 0,
  // XL_DualC_EN bit3 (0), bit2 must be 0, FS_XL[1:0] bits1..0
  uint8_t ctrl8 = (uint8_t)((uint8_t)fs_xl & 0x03);
  if (!writeReg(REG_CTRL8, ctrl8)) return false;

  return true;
}

void LSM6DSV::setIntPinConfig(bool activeLow, bool openDrain) {
  uint8_t ctrl3 = 0;
  if (!readReg(REG_CTRL3, ctrl3)) return;
  ctrl3 &= ~((1u << 5) | (1u << 4));
  if (activeLow)  ctrl3 |= (1u << 5);
  if (openDrain)  ctrl3 |= (1u << 4);
  writeReg(REG_CTRL3, ctrl3);
}

bool LSM6DSV::routeDrdyToInt1(bool accel, bool gyro) {
  uint8_t v = (accel ? (1u << 0) : 0u) | (gyro ? (1u << 1) : 0u);
  return writeReg(REG_INT1_CTRL, v);
}

bool LSM6DSV::readRaw(SampleRaw& s) {
  uint8_t buf[14] = {0};
  if (!readBytes(REG_OUT_TEMP_L, buf, sizeof(buf))) return false;

  auto le16 = [&](uint8_t l, uint8_t h) -> int16_t {
    return (int16_t)((uint16_t)l | ((uint16_t)h << 8));
  };

  s.temp = le16(buf[0],  buf[1]);
  s.gx   = le16(buf[2],  buf[3]);
  s.gy   = le16(buf[4],  buf[5]);
  s.gz   = le16(buf[6],  buf[7]);
  s.ax   = le16(buf[8],  buf[9]);
  s.ay   = le16(buf[10], buf[11]);
  s.az   = le16(buf[12], buf[13]);
  return true;
}

float LSM6DSV::accel_g_per_lsb() const {
  // LA_So: 2g=0.061 mg/LSB, 4g=0.122, 8g=0.244, 16g=0.488
  float mg_per_lsb = 0.488f;
  switch (_fs_xl) {
    case FsXl::G_2:  mg_per_lsb = 0.061f; break;
    case FsXl::G_4:  mg_per_lsb = 0.122f; break;
    case FsXl::G_8:  mg_per_lsb = 0.244f; break;
    case FsXl::G_16: mg_per_lsb = 0.488f; break;
  }
  return mg_per_lsb / 1000.0f;
}

float LSM6DSV::gyro_dps_per_lsb() const {
  // G_So: 125=4.375 mdps/LSB, 250=8.75, 500=17.5, 1000=35, 2000=70, 4000=140
  float mdps_per_lsb = 140.0f;
  switch (_fs_g) {
    case FsG::DPS_125:  mdps_per_lsb = 4.375f; break;
    case FsG::DPS_250:  mdps_per_lsb = 8.75f;  break;
    case FsG::DPS_500:  mdps_per_lsb = 17.5f;  break;
    case FsG::DPS_1000: mdps_per_lsb = 35.0f;  break;
    case FsG::DPS_2000: mdps_per_lsb = 70.0f;  break;
    case FsG::DPS_4000: mdps_per_lsb = 140.0f; break;
  }
  return mdps_per_lsb / 1000.0f;
}

bool LSM6DSV::readAccel_g(float& ax, float& ay, float& az) {
  SampleRaw s{};
  if (!readRaw(s)) return false;
  float k = accel_g_per_lsb();
  ax = (float)s.ax * k;
  ay = (float)s.ay * k;
  az = (float)s.az * k;
  return true;
}

bool LSM6DSV::readGyro_dps(float& gx, float& gy, float& gz) {
  SampleRaw s{};
  if (!readRaw(s)) return false;
  float k = gyro_dps_per_lsb();
  gx = (float)s.gx * k;
  gy = (float)s.gy * k;
  gz = (float)s.gz * k;
  return true;
}

bool LSM6DSV::readReg(uint8_t reg, uint8_t& val) {
  if (!_wire) return false;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;
  if (_wire->requestFrom((int)_addr, 1, (int)true) != 1) return false;
  val = _wire->read();
  return true;
}

bool LSM6DSV::writeReg(uint8_t reg, uint8_t val) {
  if (!_wire) return false;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(val);
  return (_wire->endTransmission(true) == 0);
}

bool LSM6DSV::readBytes(uint8_t start_reg, uint8_t* buf, size_t len) {
  if (!_wire || !buf || len == 0) return false;
  _wire->beginTransmission(_addr);
  _wire->write(start_reg);
  if (_wire->endTransmission(false) != 0) return false;

  size_t got = _wire->requestFrom((int)_addr, (int)len, (int)true);
  if (got != len) return false;

  for (size_t i = 0; i < len; i++) buf[i] = _wire->read();
  return true;
}
