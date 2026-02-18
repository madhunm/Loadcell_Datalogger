#include <Arduino.h>

#include "services/adc_frames.h"
#include "services/sd_logger.h"
#include "services/scope_stream.h"
#include "services/serial_cli.h"
#include "services/sensors_task.h"
#include "services/ui_state.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  setCpuFrequencyMhz(240);

  Serial.println("# Parachute Logger boot");

  scope_init();
  start_cli_task();
  start_logger_task();

  start_adc_frames();
  start_sensors_task();

  ui_init();
  Serial.println("# Ready.");
}

void loop() {
  ui_tick(millis());
  vTaskDelay(pdMS_TO_TICKS(10));
}
