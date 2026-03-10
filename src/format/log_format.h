#pragma once
#include <cstdint>

static constexpr uint32_t PDL_MAGIC = 0x314C4450; // "PDL1" little-endian

#pragma pack(push, 1)

struct PdlHeaderV1 {
  uint32_t magic;          // PDL_MAGIC
  uint16_t header_ver;     // 1
  uint16_t header_size;    // sizeof(PdlHeaderV1)
  uint16_t frame_ver;      // 1 or 2; 2 when IMU sample time present (PdlFrameV2)
  uint16_t frame_size;     // sizeof(PdlFrameV1) or sizeof(PdlFrameV2)
  uint32_t build_id;       // optional

  uint32_t adc_rate_hz;    // 64000
  uint32_t frame_rate_hz;  // 500
  uint32_t decim;          // 128

  uint64_t start_mono_us;  // esp_timer_get_time() at session start
  uint32_t start_rtc_epoch;// 0 if invalid
  uint32_t start_rtc_ms;   // 0 if unused

  // force_mN = slope_mN_per_code * (adc_code - tare_adc_code) + offset_mN
  float slope_mN_per_code;
  float offset_mN;

  float accel_g_per_lsb;
  float gyro_dps_per_lsb;

  int32_t overload_mN;
  int32_t underload_mN;
  int32_t compression_mN;

  uint16_t tare_frames;
  int32_t  tare_adc_code;

  uint32_t flags_static;

  uint32_t header_crc32;   // set 0 for now
  uint8_t  reserved[170];  // pad to 256 bytes
};

struct PdlFrameV1 {
  uint32_t sample_index;
  uint64_t t_us;           // timestamp of first ADC sample in this 2ms window

  int32_t adc_mean;
  int32_t adc_peak;
  int32_t adc_min;

  int32_t force_mean_mN;
  int32_t force_peak_mN;
  int32_t force_min_mN;

  int16_t ax, ay, az;
  int16_t gx, gy, gz;

  uint16_t flags;

  uint16_t vbat_mV;
  uint16_t soc_centiPct;

  uint16_t pad;            // keep struct at 56 bytes
};

struct PdlFrameV2 {
  PdlFrameV1 v1;
  uint64_t imu_sample_t_us;
};

#pragma pack(pop)

static_assert(sizeof(PdlHeaderV1) == 256, "Header must be 256 bytes");
static_assert(sizeof(PdlFrameV1) == 56, "Frame must be 56 bytes");
static_assert(sizeof(PdlFrameV2) == 64, "Frame V2 must be 64 bytes");
