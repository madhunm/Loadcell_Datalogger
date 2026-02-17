#include "drivers/lsm6dsv320x.h"
#include "drivers/i2c_util.h"
#include "esp_timer.h"

namespace {
static constexpr uint8_t REG_WHO_AM_I = 0x0F; // fixed 0x73
static constexpr uint8_t REG_CTRL1    = 0x10; // accel ODR/opmode
static constexpr uint8_t REG_CTRL2    = 0x11; // gyro  ODR/opmode
static constexpr uint8_t REG_CTRL3    = 0x12; // BDU, IF_INC, SW_RESET
static constexpr uint8_t REG_CTRL6    = 0x15; // gyro FS + LPF1 bw; bit3 must be 1
static constexpr uint8_t REG_CTRL8    = 0x17; // accel FS + filter

static constexpr uint8_t REG_OUTX_L_G = 0x22; // gyro xyz (0x22..0x27)
static constexpr uint8_t REG_OUTX_L_A = 0x28; // accel xyz (0x28..0x2D)
} // namespace

bool LSM6DSV320X::begin(TwoWire& w, uint8_t i2c_addr) {
  w_ = &w;
  addr_ = i2c_addr;

  uint8_t who = 0;
  if (!i2c_util::readReg8(*w_, addr_, REG_WHO_AM_I, &who)) return false;
  return (who == 0x73);
}

bool LSM6DSV320X::reset() {
  if (!w_) return false;
  // CTRL3: [BOOT][BDU][0][0][0][IF_INC][0][SW_RESET]
  // Set SW_RESET=1, keep other reserved bits 0; BDU/IF_INC will be set in configure().
  if (!i2c_util::writeReg8(*w_, addr_, REG_CTRL3, 0x01)) return false;
  delay(20);
  return true;
}

uint8_t LSM6DSV320X::odrToBits_(uint16_t odr_hz) {
  // matches the datasheet ODR tables:
  // 0: powerdown
  // 1: 1.875
  // 2: 7.5
  // 3: 15
  // 4: 30
  // 5: 60
  // 6: 120
  // 7: 240
  // 8: 480
  // 9: 960
  // A: 1.92k
  // B: 3.84k
  // C: 7.68k (gyro), accel continues per datasheet
  if (odr_hz >= 7600) return 0xC;
  if (odr_hz >= 3800) return 0xB;
  if (odr_hz >= 1900) return 0xA;
  if (odr_hz >=  960) return 0x9;
  if (odr_hz >=  480) return 0x8;
  if (odr_hz >=  240) return 0x7;
  if (odr_hz >=  120) return 0x6;
  if (odr_hz >=   60) return 0x5;
  if (odr_hz >=   30) return 0x4;
  if (odr_hz >=   15) return 0x3;
  return 0x2; // 7.5
}

bool LSM6DSV320X::configure(uint16_t odr_hz, AccelFS a_fs, GyroFS g_fs) {
  if (!w_) return false;

  // CTRL3: set BDU=1 and IF_INC=1 (0b0100_0100 = 0x44)
  if (!i2c_util::writeReg8(*w_, addr_, REG_CTRL3, 0x44)) return false;

  // CTRL6 (0x15): bit3 MUST be 1; FS_G in bits[2:0]
  // bits[6:4] LPF1_G_BW = 0 for now
  uint8_t ctrl6 = 0x08 | (uint8_t(g_fs) & 0x07);
  if (!i2c_util::writeReg8(*w_, addr_, REG_CTRL6, ctrl6)) return false;

  // CTRL8 (0x17): FS_XL in bits[1:0], filter bits[7:5]=0 => LPF default
  uint8_t ctrl8 = (uint8_t(a_fs) & 0x03);
  if (!i2c_util::writeReg8(*w_, addr_, REG_CTRL8, ctrl8)) return false;

  // CTRL1 (0x10): bit7 must be 0; OP_MODE_XL[2:0]=000 (high-perf); ODR_XL[3:0]
  uint8_t odr = odrToBits_(odr_hz);
  uint8_t ctrl1 = odr;
  if (!i2c_util::writeReg8(*w_, addr_, REG_CTRL1, ctrl1)) return false;

  // CTRL2 (0x11): bit7 must be 0; OP_MODE_G[2:0]=000 (high-perf); ODR_G[3:0]
  uint8_t ctrl2 = odr;
  if (!i2c_util::writeReg8(*w_, addr_, REG_CTRL2, ctrl2)) return false;

  // Scale factors (raw int16)
  auto fs_to_g = [&](AccelFS fs)->float {
    switch(fs){
      case AccelFS::FS_2G:  return 2.0f;
      case AccelFS::FS_4G:  return 4.0f;
      case AccelFS::FS_8G:  return 8.0f;
      default:              return 16.0f;
    }
  };
  auto fs_to_dps = [&](GyroFS fs)->float {
    switch(fs){
      case GyroFS::FS_250:  return 250.0f;
      case GyroFS::FS_500:  return 500.0f;
      case GyroFS::FS_1000: return 1000.0f;
      case GyroFS::FS_2000: return 2000.0f;
      default:              return 4000.0f;
    }
  };

  accel_g_per_lsb_ = fs_to_g(a_fs) / 32768.0f;
  gyro_dps_per_lsb_ = fs_to_dps(g_fs) / 32768.0f;

  return true;
}

bool LSM6DSV320X::readSample(Sample* out) {
  if (!w_ || !out) return false;

  // burst read gyro + accel: 0x22..0x2D (12+? actually 0x22..0x2D is 12 bytes)
  uint8_t buf[12];
  if (!i2c_util::readBytes(*w_, addr_, REG_OUTX_L_G, buf, sizeof(buf))) return false;

  auto le16 = [&](int i)->int16_t { return (int16_t)((uint16_t)buf[i] | ((uint16_t)buf[i+1] << 8)); };

  out->t_us = (uint64_t)esp_timer_get_time();
  out->raw.gx = le16(0);
  out->raw.gy = le16(2);
  out->raw.gz = le16(4);
  out->raw.ax = le16(6);
  out->raw.ay = le16(8);
  out->raw.az = le16(10);
  return true;
}
