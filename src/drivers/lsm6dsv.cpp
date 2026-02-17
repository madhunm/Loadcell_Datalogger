#include "lsm6dsv.h"

static constexpr float G0 = 9.80665f;

bool LSM6DSV::begin(TwoWire &wire, uint8_t addr) {
  _wire = &wire;

  // Try provided addr, then the other one (0x6A <-> 0x6B)
  for (int i = 0; i < 2; i++) {
    _addr = addr;

    uint8_t v = 0;
    if (readReg(REG_WHO_AM_I, v)) {
      _whoami = v;
      if (_whoami == 0x70) { // LSM6DSV WhoAmI
        return true;
      }
    }
    addr = (addr == 0x6A) ? 0x6B : 0x6A;
  }

  return false;
}

bool LSM6DSV::configureMaxFS(ODR odr) {
  if (!_wire) return false;

  // Ensure OIS chain disabled (required when selecting FS=±4000 dps per datasheet note)
  if (!writeReg(REG_UI_CTRL1_OIS, 0x00)) return false;

  // CTRL3: set BDU=1 and IF_INC=1 (multi-byte stable + auto-increment)
  uint8_t ctrl3 = 0;
  if (!readReg(REG_CTRL3, ctrl3)) return false;
  ctrl3 |= (1u << 6); // BDU
  ctrl3 |= (1u << 2); // IF_INC
  if (!writeReg(REG_CTRL3, ctrl3)) return false;

  // CTRL8: accel FS to max (±16g). Keep filters default (0), dual-channel disabled.
  _afs = AccelFS::g16;
  uint8_t ctrl8 = 0;
  ctrl8 |= (static_cast<uint8_t>(_afs) & 0x03); // FS_XL[1:0]
  if (!writeReg(REG_CTRL8, ctrl8)) return false;

  // CTRL6: gyro FS to max (±4000 dps). LPF BW default = 0.
  _gfs = GyroFS::dps4000;
  uint8_t ctrl6 = 0;
  ctrl6 |= (static_cast<uint8_t>(_gfs) & 0x0F); // FS_G[3:0]
  if (!writeReg(REG_CTRL6, ctrl6)) return false;

  // CTRL1 / CTRL2: set ODR, opmode = high-performance (000)
  const uint8_t odrBits = static_cast<uint8_t>(odr) & 0x0F;
  const uint8_t opModeHP = 0x0; // OP_MODE_* = 000b

  uint8_t ctrl1 = (opModeHP << 4) | odrBits; // accel
  uint8_t ctrl2 = (opModeHP << 4) | odrBits; // gyro

  if (!writeReg(REG_CTRL1, ctrl1)) return false;
  if (!writeReg(REG_CTRL2, ctrl2)) return false;

  return true;
}

bool LSM6DSV::readRaw(SampleRaw &out) {
  uint8_t buf[14] = {0};
  if (!readRegs(REG_OUT_TEMP_L, buf, sizeof(buf))) return false;

  auto rd16 = [&](int idx) -> int16_t {
    return (int16_t)((uint16_t)buf[idx] | ((uint16_t)buf[idx + 1] << 8));
  };

  out.temp = rd16(0);
  out.gx   = rd16(2);
  out.gy   = rd16(4);
  out.gz   = rd16(6);
  out.ax   = rd16(8);
  out.ay   = rd16(10);
  out.az   = rd16(12);

  return true;
}

bool LSM6DSV::readSI(SampleSI &out) {
  SampleRaw r{};
  if (!readRaw(r)) return false;

  // Temperature conversion isn’t included in the snippets we relied on here,
  // so we keep it raw->degC as "unknown" unless you want me to pin it to the datasheet section.
  out.temp_c = NAN;

  const float a_g = accel_g_per_lsb();
  const float g_dps = gyro_dps_per_lsb();

  out.gx_dps = r.gx * g_dps;
  out.gy_dps = r.gy * g_dps;
  out.gz_dps = r.gz * g_dps;

  out.ax_ms2 = r.ax * a_g * G0;
  out.ay_ms2 = r.ay * a_g * G0;
  out.az_ms2 = r.az * a_g * G0;

  return true;
}

float LSM6DSV::accel_g_per_lsb() const {
  // From datasheet table: LA_So (mg/LSB). Convert mg -> g.
  switch (_afs) {
    case AccelFS::g2:  return 0.061f  * 1e-3f;
    case AccelFS::g4:  return 0.122f  * 1e-3f;
    case AccelFS::g8:  return 0.244f  * 1e-3f;
    case AccelFS::g16: return 0.488f  * 1e-3f;
  }
  return 0.488f * 1e-3f;
}

float LSM6DSV::gyro_dps_per_lsb() const {
  // From datasheet table: G_So (mdps/LSB). Convert mdps -> dps.
  switch (_gfs) {
    case GyroFS::dps125:  return 4.375f * 1e-3f;
    case GyroFS::dps250:  return 8.75f  * 1e-3f;
    case GyroFS::dps500:  return 17.5f  * 1e-3f;
    case GyroFS::dps1000: return 35.0f  * 1e-3f;
    case GyroFS::dps2000: return 70.0f  * 1e-3f;
    case GyroFS::dps4000: return 140.0f * 1e-3f;
  }
  return 140.0f * 1e-3f;
}

bool LSM6DSV::writeReg(uint8_t reg, uint8_t val) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(val);
  return (_wire->endTransmission(true) == 0);
}

bool LSM6DSV::readReg(uint8_t reg, uint8_t &val) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;

  if (_wire->requestFrom((int)_addr, 1, (int)true) != 1) return false;
  val = _wire->read();
  return true;
}

bool LSM6DSV::readRegs(uint8_t startReg, uint8_t *buf, size_t len) {
  _wire->beginTransmission(_addr);
  _wire->write(startReg);
  if (_wire->endTransmission(false) != 0) return false;

  size_t got = _wire->requestFrom((int)_addr, (int)len, (int)true);
  if (got != len) return false;

  for (size_t i = 0; i < len; i++) buf[i] = _wire->read();
  return true;
}
