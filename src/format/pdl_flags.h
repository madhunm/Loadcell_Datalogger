#pragma once
#include <cstdint>

enum PdlFlags : uint16_t {
  FLG_OVERLOAD        = 1u << 0,
  FLG_UNDERLOAD       = 1u << 1,
  FLG_COMPRESSION     = 1u << 2,

  FLG_IMU_SAT         = 1u << 3,
  FLG_IMU_FAULT       = 1u << 4,
  FLG_RTC_INVALID     = 1u << 5,

  FLG_SD_WARN         = 1u << 6,
  FLG_SD_FAIL         = 1u << 7,

  FLG_I2C_RECOVERED   = 1u << 8,
  FLG_LOW_BATT        = 1u << 9,

  FLG_DROPPED_FRAME   = 1u << 10,
  FLG_MARK            = 1u << 11,
};
