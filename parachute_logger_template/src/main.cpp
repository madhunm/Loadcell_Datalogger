#include <Arduino.h>

#include "services/adc_frames.h"
#include "services/sd_logger.h"
#include "services/scope_stream.h"
#include "services/serial_cli.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  setCpuFrequencyMhz(240);

  Serial.println("# Parachute Logger Template boot");

  scope_init();
  start_cli_task();

  if (!logger_begin()) {
    Serial.println("#ERR: SD init failed (logger_begin). Logging will not work until fixed.");
  }
  start_logger_task();

  // Start ADC frame producer (64 ksps acquisition -> 500 Hz frames)
  start_adc_frames();

  Serial.println("# Ready.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
