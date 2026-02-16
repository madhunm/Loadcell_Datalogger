#include <Arduino.h>
#include <limits>

#include "drivers/max11270.h"
#include "format/log_format.h"
#include "format/pdl_flags.h"
#include "services/frame_pipe.h"

static constexpr int ADC_HZ = 64000;
static constexpr int FRAME_HZ = 500;
static constexpr int DECIM = ADC_HZ / FRAME_HZ; // 128

static Max11270 adc;

// Placeholder calibration: force_mN = slope * (code - tare) + offset
static float g_slope_mN_per_code = 1.0f;
static float g_offset_mN = 0.0f;
static int32_t g_tare_code = 0;

struct LatestAux {
  int16_t ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
  uint16_t vbat_mV=0;
  uint16_t soc_centiPct=0;
};
static LatestAux g_aux;

static inline int32_t code_to_force_mN(int32_t code) {
  float f = g_slope_mN_per_code * (float)(code - g_tare_code) + g_offset_mN;
  if (f > (float)INT32_MAX) return INT32_MAX;
  if (f < (float)INT32_MIN) return INT32_MIN;
  return (int32_t)lroundf(f);
}

static void adc_frame_task(void*) {
  Max11270::BusConfig bus { .host=SPI2_HOST, .dma_chan=SPI_DMA_CH_AUTO, .clock_hz=5'000'000, .init_bus=true, .queue_size=1 };
  Max11270::SpiPins spi { .mosi=GPIO_NUM_13, .miso=GPIO_NUM_12, .sclk=GPIO_NUM_18, .cs=GPIO_NUM_17 };
  Max11270::GpioPins gp { .rdyb=GPIO_NUM_16, .rstb=GPIO_NUM_15, .sync=GPIO_NUM_14 };

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

    for (int i=0; i<DECIM; i++) {
      Max11270::Sample smp;
      ESP_ERROR_CHECK(adc.readSampleBlocking(&smp, 0));
      if (i == 0) t_first = smp.t_us;

      const int32_t code = smp.code;
      sum += code;
      if (code < mn) mn = code;
      if (code > mx) mx = code;
    }

    const int32_t mean = (int32_t)(sum / DECIM);

    PdlFrameV1 fr{};
    fr.sample_index = frame_idx++;
    fr.t_us = t_first;

    fr.adc_mean = mean;
    fr.adc_peak = mx;
    fr.adc_min  = mn;

    fr.force_mean_mN = code_to_force_mN(mean);
    fr.force_peak_mN = code_to_force_mN(mx);
    fr.force_min_mN  = code_to_force_mN(mn);

    fr.ax = g_aux.ax; fr.ay = g_aux.ay; fr.az = g_aux.az;
    fr.gx = g_aux.gx; fr.gy = g_aux.gy; fr.gz = g_aux.gz;
    fr.vbat_mV = g_aux.vbat_mV;
    fr.soc_centiPct = g_aux.soc_centiPct;

    fr.flags = 0;
    fr.pad = 0;

    if (g_frame_q) (void)xQueueSend(g_frame_q, &fr, 0);
  }
}

void start_adc_frames() {
  g_frame_q = xQueueCreate(600, sizeof(PdlFrameV1));
  xTaskCreatePinnedToCore(adc_frame_task, "adc_frame", 6144, nullptr, configMAX_PRIORITIES-2, nullptr, 1);
}
