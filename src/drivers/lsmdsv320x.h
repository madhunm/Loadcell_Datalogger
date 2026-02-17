#pragma once
#include <Arduino.h>
#include "i2c_dev.h"

struct ImuSample {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
};

class LSM6DSV320X {
public:
  // SA0 low => 0x6A, SA0 high => 0x6B (we’ll auto-try both)
  explicit LSM6DSV320X(TwoWire& w = Wire) : wire(w), addr(0x6A), dev(w, 0x6A) {}

  bool begin(uint8_t accelOdrBits = 0x08 /*480Hz*/, uint8_t gyroOdrBits = 0x08 /*480Hz*/,
             uint8_t fs_xl = 0x00 /*±2g*/, uint8_t fs_g = 0x03 /*±1000dps*/) {
    // Try both addresses
    if (!probeAt(0x6A) && !probeAt(0x6B)) return false;

    // WHO_AM_I = 0x0F, expected 0x73 for LSM6DSV320X (per datasheet)
    uint8_t who = 0;
    if (!readReg(0x0F, who) || who != 0x73) return false;

    // CTRL3 (0x12): IF_INC=1 (0x04) + SW_RESET=0 + BDU optional
    // Datasheet default is 0x44; we’ll enforce auto-increment.
    writeReg(0x12, 0x44);

    // CTRL8 (0x17): FS_XL[1:0] in bits [1:0]
    // Keep the "must be 0" bit as 0; only set FS bits.
    writeReg(0x17, (fs_xl & 0x03));

    // CTRL6 (0x15): bit3 must be 1 for correct operation; FS_G[2:0] in [2:0]
    writeReg(0x15, 0x08 | (fs_g & 0x07));

    // CTRL1 (0x10): ODR_XL[3:0]
    writeReg(0x10, (accelOdrBits & 0x0F));

    // CTRL2 (0x11): ODR_G[3:0]
    writeReg(0x11, (gyroOdrBits & 0x0F));

    return true;
  }

  bool readSample(ImuSample& s) {
    uint8_t buf[12];
    // OUTX_L_G starts at 0x22; auto-increment reads GX..GZ then AX..AZ
    if (!readBytes(0x22, buf, sizeof(buf))) return false;

    s.gx = (int16_t)((buf[1] << 8) | buf[0]);
    s.gy = (int16_t)((buf[3] << 8) | buf[2]);
    s.gz = (int16_t)((buf[5] << 8) | buf[4]);

    s.ax = (int16_t)((buf[7] << 8) | buf[6]);
    s.ay = (int16_t)((buf[9] << 8) | buf[8]);
    s.az = (int16_t)((buf[11] << 8) | buf[10]);
    return true;
  }

private:
  bool probeAt(uint8_t a) {
    I2CDev tmp(wire, a);
    if (!tmp.ping()) return false;
    addr = a;
    dev = I2CDev(wire, addr);
    return true;
  }

  bool writeReg(uint8_t reg, uint8_t v) { return dev.writeBytes(reg, &v, 1); }
  bool readReg(uint8_t reg, uint8_t& v) { return dev.readBytes(reg, &v, 1); }
  bool readBytes(uint8_t reg, uint8_t* data, size_t len) { return dev.readBytes(reg, data, len); }

  TwoWire& wire;
  uint8_t addr;
  I2CDev dev;
};
