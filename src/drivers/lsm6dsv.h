#pragma once
#include <Arduino.h>
#include <Wire.h>

class LSM6DSV {
public:
  struct SampleRaw {
    int16_t temp;
    int16_t gx, gy, gz;
    int16_t ax, ay, az;
  };

  enum class Odr : uint8_t {
    POWER_DOWN = 0x0,
    HZ_1_875   = 0x1,
    HZ_7_5     = 0x2,
    HZ_15      = 0x3,
    HZ_30      = 0x4,
    HZ_60      = 0x5,
    HZ_120     = 0x6,
    HZ_240     = 0x7,
    HZ_480     = 0x8,
    HZ_960     = 0x9,
    KHZ_1_92   = 0xA,
    KHZ_3_84   = 0xB,
    KHZ_7_68   = 0xC
  };

  enum class FsXl : uint8_t { G_2 = 0, G_4 = 1, G_8 = 2, G_16 = 3 };

  // FS_G codes per datasheet (CTRL6 FS_G[3:0]).
  enum class FsG : uint8_t {
    DPS_125  = 0x0,
    DPS_250  = 0x1,
    DPS_500  = 0x2,
    DPS_1000 = 0x3,
    DPS_2000 = 0x4,
    DPS_4000 = 0xC
  };

  explicit LSM6DSV(uint8_t i2c_addr = 0x6A) : _addr(i2c_addr) {}

  // If you pass addr 0x6A and probe fails, begin() will also try 0x6B automatically.
  bool begin(TwoWire& wire = Wire, uint8_t addr = 0x6A);

  bool configure(
    Odr odr_xl = Odr::HZ_960,
    Odr odr_g  = Odr::HZ_960,
    FsXl fs_xl = FsXl::G_16,
    FsG  fs_g  = FsG::DPS_4000,
    bool enable_gyro_lpf1 = false,
    uint8_t gyro_lpf1_bw  = 0 // 0..7 into CTRL6 LPF1_G_BW[2:0]
  );

  bool readWhoAmI(uint8_t& whoami);
  bool readRaw(SampleRaw& s);

  void setIntPinConfig(bool activeLow = false, bool openDrain = false);
  bool routeDrdyToInt1(bool accel, bool gyro);

  // Conversions using datasheet sensitivities.
  float accel_g_per_lsb() const;
  float gyro_dps_per_lsb() const;

  bool readAccel_g(float& ax, float& ay, float& az);
  bool readGyro_dps(float& gx, float& gy, float& gz);

private:
  TwoWire* _wire = nullptr;
  uint8_t _addr = 0x6A;

  Odr  _odr_xl = Odr::POWER_DOWN;
  Odr  _odr_g  = Odr::POWER_DOWN;
  FsXl _fs_xl  = FsXl::G_16;
  FsG  _fs_g   = FsG::DPS_4000;

  static constexpr uint8_t REG_WHO_AM_I = 0x0F;
  static constexpr uint8_t REG_CTRL1    = 0x10;
  static constexpr uint8_t REG_CTRL2    = 0x11;
  static constexpr uint8_t REG_CTRL3     = 0x12;
  static constexpr uint8_t REG_INT1_CTRL = 0x0D;
  static constexpr uint8_t REG_CTRL6    = 0x15;
  static constexpr uint8_t REG_CTRL7    = 0x16;
  static constexpr uint8_t REG_CTRL8    = 0x17;

  static constexpr uint8_t REG_OUT_TEMP_L = 0x20; // temp + gyro + accel starts here

  static constexpr uint8_t WHOAMI_EXPECTED = 0x70;

  bool softReset();

  bool readReg(uint8_t reg, uint8_t& val);
  bool writeReg(uint8_t reg, uint8_t val);
  bool readBytes(uint8_t start_reg, uint8_t* buf, size_t len);
};
