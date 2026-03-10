#include <Arduino.h>
#include <limits>

#include "config.h"
#include "drivers/max11270.h"
#include "format/log_format.h"
#include "format/pdl_flags.h"
#include "services/frame_pipe.h"
#include "services/aux_state.h"
#include "services/sd_logger.h"
#include "services/system_status.h"
#include "pins.h"

static constexpr int ADC_HZ = 64000;
static constexpr int FRAME_HZ = 500;
static constexpr int DECIM = ADC_HZ / FRAME_HZ; // 128

static Max11270 adc;

// Placeholder calibration: force_mN = slope * (code - tare) + offset
static float g_slope_mN_per_code = 1.0f;
static float g_offset_mN = 0.0f;
static int32_t g_tare_code = 0;

static volatile bool s_tare_phase = false;
static int64_t s_tare_sum = 0;
static uint32_t s_tare_count = 0;
static uint32_t s_last_queue_pressure_log_ms = 0;
static uint32_t s_last_queue_drop_log_ms = 0;

static int32_t s_overload_mN = std::numeric_limits<int32_t>::max();
static int32_t s_underload_mN = std::numeric_limits<int32_t>::min();
static int32_t s_compression_mN = std::numeric_limits<int32_t>::max();

void adc_frames_on_session_start(const PdlHeaderV1& hdr) {
  g_slope_mN_per_code = hdr.slope_mN_per_code;
  g_offset_mN = hdr.offset_mN;
  if (hdr.tare_frames > 0) {
    g_tare_code = hdr.tare_adc_code;
    s_tare_phase = false;
  } else {
    s_tare_phase = true;
  }
  s_tare_sum = 0;
  s_tare_count = 0;
  s_overload_mN = hdr.overload_mN;
  s_underload_mN = hdr.underload_mN;
  s_compression_mN = hdr.compression_mN;
}

static inline int32_t code_to_force_mN(int32_t code) {
  float f = g_slope_mN_per_code * (float)(code - g_tare_code) + g_offset_mN;
  if (f > (float)INT32_MAX) return INT32_MAX;
  if (f < (float)INT32_MIN) return INT32_MIN;
  return (int32_t)lroundf(f);
}

static void adc_frame_task(void*) {
  Max11270::BusConfig bus;
  bus.host = SPI2_HOST;
  bus.dma_chan = SPI_DMA_CH_AUTO;
  bus.clock_hz = 5000000;
  bus.init_bus = true;
  bus.queue_size = 1;

  Max11270::SpiPins spi;
  spi.mosi = (gpio_num_t)PIN_ADC_MOSI;
  spi.miso = (gpio_num_t)PIN_ADC_MISO;
  spi.sclk = (gpio_num_t)PIN_ADC_SCK;
  spi.cs   = (gpio_num_t)PIN_ADC_CS;

  Max11270::GpioPins gp;
  gp.rdyb = (gpio_num_t)PIN_ADC_RDYB;
  gp.rstb = (gpio_num_t)PIN_ADC_RSTB;
  gp.sync = (gpio_num_t)PIN_ADC_SYNC;

  ESP_ERROR_CHECK(adc.begin(bus, spi, gp));
  ESP_ERROR_CHECK(adc.hardwareReset(2, 5));
  ESP_ERROR_CHECK(adc.softwareReset(5));

  Max11270::Settings s;
  s.rate = Max11270::Rate::R_64000SPS;
  s.use_internal_clock = true;      // ADC CLK pin grounded -> internal clock required
  s.continuous_conversion = true;   // SCYCLE=0
  s.data32 = true;
  s.enable_pga = true;
  s.pga_gain = Max11270::PgaGain::X128;

  ESP_ERROR_CHECK(adc.configure(s));
  ESP_ERROR_CHECK(adc.selfCalibrate());
  ESP_ERROR_CHECK(adc.startConversions(s.rate));

  uint32_t frame_idx = 0;

  while (true) {
    int64_t sum = 0;
    int32_t mn = INT32_MAX;
    int32_t mx = INT32_MIN;
    uint64_t t_first = 0;
    bool adc_error = false;

    for (int i=0; i<DECIM; i++) {
      Max11270::Sample smp;
      esp_err_t err = adc.readSampleBlocking(&smp, 0);
      if (err != ESP_OK) {
        system_status_set_fault(FaultCode::ADC_FAULT);
        adc_error = true;
        break;
      }
      if (i == 0) t_first = smp.t_us;

      const int32_t code = smp.code;
      sum += code;
      if (code < mn) mn = code;
      if (code > mx) mx = code;
    }

    if (adc_error) continue;

    const int32_t mean = (int32_t)(sum / DECIM);

    if (s_tare_phase && logger_is_logging()) {
      s_tare_sum += mean;
      s_tare_count++;
      if (s_tare_count >= TARE_N) {
        g_tare_code = (int32_t)(s_tare_sum / (int64_t)TARE_N);
        logger_set_tare_result((uint16_t)TARE_N, g_tare_code, (uint16_t)(TARE_N * 2));
        s_tare_phase = false;
      }
    }

    PdlFrameV2 fr{};
    fr.v1.sample_index = frame_idx++;
    fr.v1.t_us = t_first;

    fr.v1.adc_mean = mean;
    fr.v1.adc_peak = mx;
    fr.v1.adc_min  = mn;

    fr.v1.force_mean_mN = code_to_force_mN(mean);
    fr.v1.force_peak_mN = code_to_force_mN(mx);
    fr.v1.force_min_mN  = code_to_force_mN(mn);

    AuxSnapshot snap = aux_get_snapshot();
    fr.v1.ax = snap.ax; fr.v1.ay = snap.ay; fr.v1.az = snap.az;
    fr.v1.gx = snap.gx; fr.v1.gy = snap.gy; fr.v1.gz = snap.gz;
    fr.v1.vbat_mV = snap.vbat_mV;
    fr.v1.soc_centiPct = snap.soc_centiPct;

    fr.v1.flags = 0;
    if (frame_pipe_consume_mark_next()) fr.v1.flags |= FLG_MARK;
    uint32_t now_ms = (uint32_t)(t_first / 1000);
    if (frame_pipe_should_set_dropped(now_ms)) fr.v1.flags |= FLG_DROPPED_FRAME;
    if (frame_pipe_should_set_sd_warn(now_ms)) fr.v1.flags |= FLG_SD_WARN;
    if (!snap.imu_valid) { fr.v1.flags |= FLG_IMU_FAULT; system_status_set_warning(WarningCode::IMU_WARN); } else { system_status_clear_warning(WarningCode::IMU_WARN); }
    if (fr.v1.force_peak_mN > s_overload_mN) { fr.v1.flags |= FLG_OVERLOAD; system_status_set_warning(WarningCode::OVERLOAD); } else { system_status_clear_warning(WarningCode::OVERLOAD); }
    if (fr.v1.force_mean_mN < s_underload_mN) { fr.v1.flags |= FLG_UNDERLOAD; system_status_set_warning(WarningCode::UNDERLOAD); } else { system_status_clear_warning(WarningCode::UNDERLOAD); }
    if (fr.v1.force_min_mN < -s_compression_mN) { fr.v1.flags |= FLG_COMPRESSION; system_status_set_warning(WarningCode::COMPRESSION); } else { system_status_clear_warning(WarningCode::COMPRESSION); }
    if (!snap.rtc_valid) { fr.v1.flags |= FLG_RTC_INVALID; system_status_set_warning(WarningCode::RTC_INVALID); } else { system_status_clear_warning(WarningCode::RTC_INVALID); }
    if (snap.soc_centiPct < LOW_BATT_SOC_CENTI || snap.vbat_mV < LOW_BATT_MV) { fr.v1.flags |= FLG_LOW_BATT; system_status_set_warning(WarningCode::LOW_BATT); } else { system_status_clear_warning(WarningCode::LOW_BATT); }
    fr.v1.pad = 0;

    fr.imu_sample_t_us = snap.imu_sample_t_us;

    if (g_frame_q) {
      UBaseType_t queued = uxQueueMessagesWaiting(g_frame_q);
      frame_pipe_note_queue_depth(now_ms, queued);
      if (queued >= FRAME_QUEUE_PRESSURE_WARN && (now_ms - s_last_queue_pressure_log_ms) >= 1000) {
        s_last_queue_pressure_log_ms = now_ms;
        Serial.printf("#WARN: frame queue pressure depth=%u max=%u drops=%lu\n",
                      (unsigned)queued,
                      (unsigned)frame_pipe_get_max_queue_depth(),
                      (unsigned long)frame_pipe_get_drop_count());
      }
      if (xQueueSend(g_frame_q, &fr, 0) != pdTRUE) {
        frame_pipe_notify_drop(now_ms);
        if ((now_ms - s_last_queue_drop_log_ms) >= 1000) {
          s_last_queue_drop_log_ms = now_ms;
          Serial.printf("#ERR: frame queue full drops=%lu max=%u\n",
                        (unsigned long)frame_pipe_get_drop_count(),
                        (unsigned)frame_pipe_get_max_queue_depth());
        }
      } else {
        frame_pipe_note_queue_depth(now_ms, uxQueueMessagesWaiting(g_frame_q));
      }
    }
  }
}

void start_adc_frames() {
  g_frame_q = xQueueCreate(FRAME_QUEUE_DEPTH, sizeof(PdlFrameV2));
  xTaskCreatePinnedToCore(adc_frame_task, "adc_frame", 6144, nullptr, configMAX_PRIORITIES-2, nullptr, 1);
}
