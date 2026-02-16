#include <Arduino.h>
#include "max11270.h"

static Max11270 adc;

static void adc_bringup_task(void*) {
  Max11270::BusConfig bus { .host=SPI2_HOST, .dma_chan=SPI_DMA_CH_AUTO, .clock_hz=5'000'000, .init_bus=true, .queue_size=1 };

  Max11270::SpiPins spi { .mosi=GPIO_NUM_13, .miso=GPIO_NUM_12, .sclk=GPIO_NUM_18, .cs=GPIO_NUM_17 };
  Max11270::GpioPins gp { .rdyb=GPIO_NUM_16, .rstb=GPIO_NUM_15, .sync=GPIO_NUM_14 };

  ESP_ERROR_CHECK(adc.begin(bus, spi, gp));
  ESP_ERROR_CHECK(adc.hardwareReset(2, 5));
  ESP_ERROR_CHECK(adc.softwareReset(5));

  Max11270::Settings s;
  s.rate = Max11270::Rate::R_64000SPS;
  s.use_internal_clock = true;     // CLK pin grounded => MUST be internal clock
  s.continuous_conversion = true;  // SCYCLE=0
  s.data32 = true;
  s.enable_pga = true;
  s.pga_gain = Max11270::PgaGain::X128;

  ESP_ERROR_CHECK(adc.configure(s));
  ESP_ERROR_CHECK(adc.selfCalibrate());
  ESP_ERROR_CHECK(adc.startConversions(s.rate));

  uint32_t cnt = 0;
  uint32_t t0 = millis();

  while (true) {
    Max11270::Sample smp;
    esp_err_t err = adc.readSampleBlocking(&smp, 1000); // 1ms timeout for debug
    if (err == ESP_OK) cnt++;
    else Serial.printf("ADC err=%d\n", (int)err);

    if (millis() - t0 >= 1000) {
      Serial.printf("ADC samples/s: %u\n", cnt);
      cnt = 0;
      t0 = millis();
    }
  }
}

void start_adc_bringup() {
  xTaskCreatePinnedToCore(adc_bringup_task, "adc_bringup", 4096, nullptr, configMAX_PRIORITIES-2, nullptr, 1);
}
