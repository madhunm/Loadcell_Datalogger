#pragma once
#include <Arduino.h>
#include <Wire.h>

class LSM6DSV {
public:
  enum class ODR : uint8_t {
    PowerDown = 0x0,
    Hz1_875   = 0x1,
    Hz7_5     = 0x2,
    Hz15      = 0x3,
    Hz30      = 0x4,
    Hz60      = 0x5,
    Hz120     = 0x6,
    Hz240     = 0x7,
    Hz480     = 0x8,
    Hz960     = 0x9,
    kHz1_92   = 0xA,
    kHz3_84   = 0xB,
    kHz7_68   = 0xC
  };

  enum class AccelFS : uint8_t { // CTRL8 FS_XL[1:0]
    g2  = 0x0,
    g4  = 0x1,
    g8  = 0x2,
    g16 = 0x3
  };

  enum class GyroFS : uint8_t { // CTRL6 FS_G[3:0]
    dps125  = 0x0,
    dps250  = 0x1,
    dps500  = 0x2,
    dps1000 = 0x3,
    dps2000 = 0x4,
    dps4000 = 0xC
  };

  struct SampleRaw {
    int16_t temp;
    int16_t gx, gy, gz;
    int16_t ax, ay, az;
  };

  struct SampleSI {
    float temp_c;
    float gx_dps, gy_dps, gz_dps;
    float ax_ms2, ay_ms2, az_ms2;
  };

  bool begin(TwoWire &wire, uint8_t addr = 0x6A);
  bool configureMaxFS(ODR odr = ODR::Hz960);

  bool readRaw(SampleRaw &out);
  bool readSI(SampleSI &out);

  uint8_t whoAmI() const { return _whoami; }
  uint8_t address() const { return _addr; }

private:
  // Registers (primary interface)
  static constexpr uint8_t REG_WHO_AM_I   = 0x0F;
  static constexpr uint8_t REG_CTRL1      = 0x10; // accel ODR + opmode
  static constexpr uint8_t REG_CTRL2      = 0x11; // gyro  ODR + opmode
  static constexpr uint8_t REG_CTRL3      = 0x12; // BDU, IF_INC
  static constexpr uint8_t REG_CTRL6      = 0x15; // gyro FS + LPF BW
  static constexpr uint8_t REG_CTRL8      = 0x17; // accel FS + filters
  static constexpr uint8_t REG_UI_CTRL1_OIS = 0x70; // clear to disable OIS chain (needed for 4000 dps)

  static constexpr uint8_t REG_OUT_TEMP_L = 0x20; // then 14 bytes through accel ZH

  TwoWire *_wire = nullptr;
  uint8_t _addr = 0;
  uint8_t _whoami = 0;

  AccelFS _afs = AccelFS::g16;
  GyroFS  _gfs = GyroFS::dps4000;

  bool writeReg(uint8_t reg, uint8_t val);
  bool readReg(uint8_t reg, uint8_t &val);
  bool readRegs(uint8_t startReg, uint8_t *buf, size_t len);

  float accel_g_per_lsb() const;
  float gyro_dps_per_lsb() const;
};
