#include "services/sd_logger.h"

#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"

#include "services/frame_pipe.h"
#include "services/scope_stream.h"

static File g_file;
static volatile bool g_logging = false;
static uint32_t next_log_id = 0;

static String make_filename() {
  char buf[32];
  snprintf(buf, sizeof(buf), "/LOG%04u.BIN", next_log_id++);
  return String(buf);
}

PdlHeaderV1 logger_make_default_header() {
  PdlHeaderV1 h{};
  h.magic = PDL_MAGIC;
  h.header_ver = 1;
  h.header_size = sizeof(PdlHeaderV1);
  h.frame_ver = 1;
  h.frame_size = sizeof(PdlFrameV1);

  h.adc_rate_hz = 64000;
  h.frame_rate_hz = 500;
  h.decim = 128;

  h.start_mono_us = (uint64_t)esp_timer_get_time();

  h.slope_mN_per_code = 1.0f;
  h.offset_mN = 0.0f;

  h.accel_g_per_lsb = 1.0f / 16384.0f;
  h.gyro_dps_per_lsb = 1.0f;

  memset(h.reserved, 0, sizeof(h.reserved));
  return h;
}

bool logger_begin() {
  // 1-bit mode often more tolerant; change to false for 4-bit if your wiring supports it.
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("#ERR: SD_MMC.begin failed");
    return false;
  }
  return true;
}

bool logger_is_logging() { return g_logging; }

bool logger_start_session(const PdlHeaderV1& hdr) {
  if (g_logging) return false;

  String fn = make_filename();
  g_file = SD_MMC.open(fn, FILE_WRITE);
  if (!g_file) {
    Serial.println("#ERR: open log file failed");
    return false;
  }

  size_t n = g_file.write((const uint8_t*)&hdr, sizeof(hdr));
  if (n != sizeof(hdr)) {
    Serial.println("#ERR: header write failed");
    g_file.close();
    return false;
  }

  g_file.flush();
  g_logging = true;

  Serial.print("#LOGFILE: ");
  Serial.println(fn);
  return true;
}

void logger_stop_session() {
  if (!g_logging) return;
  g_logging = false;
  g_file.flush();
  g_file.close();
  Serial.println("#LOGSTOP");
}

static void logger_task(void*) {
  uint32_t last_flush_ms = millis();

  while (true) {
    PdlFrameV1 fr;
    if (g_frame_q && xQueueReceive(g_frame_q, &fr, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (g_logging) {
        size_t n = g_file.write((const uint8_t*)&fr, sizeof(fr));
        if (n != sizeof(fr)) {
          Serial.println("#ERR: SD write failed");
        } else {
          // proxy-to-SD: only feed scope from frames actually written
          scope_feed_frame(fr);
        }

        uint32_t now = millis();
        if (now - last_flush_ms > 1000) {
          g_file.flush();
          last_flush_ms = now;
        }
      }
    }
  }
}

void start_logger_task() {
  xTaskCreatePinnedToCore(logger_task, "sd_logger", 6144, nullptr, configMAX_PRIORITIES-4, nullptr, 0);
}
