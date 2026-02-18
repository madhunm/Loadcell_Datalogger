#include "services/sd_logger.h"
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <cstddef>

#include "services/frame_pipe.h"
#include "services/scope_stream.h"
#include "services/adc_frames.h"

struct TareResult {
  uint16_t frames;
  int32_t adc_code;
  uint16_t duration_ms;
};
static QueueHandle_t g_tare_result_q = nullptr;

static File g_file;
static volatile bool g_logging = false;
static volatile bool g_drain_requested = false;
static char g_current_tmp_path[64] = "";
static char g_last_bin_path[64] = "";

static constexpr size_t FRAME_BUF_COUNT = 256;
static constexpr size_t FRAME_BUF_BYTES = FRAME_BUF_COUNT * sizeof(PdlFrameV2);
static constexpr size_t FLUSH_INTERVAL_BYTES = 256 * 1024;

static uint32_t next_run_number() {
  uint32_t n = 0;
  File f = SD_MMC.open("/PDL_RUN.NUM", FILE_READ);
  if (f && f.available() >= 4) {
    uint8_t b[4];
    f.read(b, 4);
    f.close();
    n = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  } else if (f) {
    f.close();
  }
  n++;
  SD_MMC.remove("/PDL_RUN.NUM");
  f = SD_MMC.open("/PDL_RUN.NUM", FILE_WRITE);
  if (f) {
    uint8_t b[4] = { (uint8_t)(n & 0xFF), (uint8_t)((n >> 8) & 0xFF), (uint8_t)((n >> 16) & 0xFF), (uint8_t)((n >> 24) & 0xFF) };
    f.write(b, 4);
    f.close();
  }
  return n;
}

static void epoch_to_ymdhmss(uint32_t epoch, int* y, int* mo, int* d, int* h, int* mi, int* s) {
  const int32_t days = (int32_t)(epoch / 86400);
  const int32_t sec = (int32_t)(epoch % 86400);
  int32_t z = days + 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = (uint32_t)(z - era * 146097);
  const int32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
  const int32_t y_ = yoe + era * 400;
  const int32_t doy = doe - (365*yoe + yoe/4 - yoe/100);
  const int32_t mp = (5*doy + 2)/153;
  *d = doy - (153*mp+2)/5 + 1;
  *mo = mp + (mp < 10 ? 3 : -9);
  *y = (int)(y_ + (*(mo) <= 2 ? 1 : 0));
  *h = (int)(sec / 3600);
  *mi = (int)((sec % 3600) / 60);
  *s = (int)(sec % 60);
}

static void make_tmp_filename(bool rtc_valid, uint32_t rtc_epoch, char* buf, size_t len) {
  if (rtc_valid && rtc_epoch > 0) {
    int y, mo, d, h, mi, s;
    epoch_to_ymdhmss(rtc_epoch, &y, &mo, &d, &h, &mi, &s);
    snprintf(buf, len, "/PDL_%04d%02d%02d_%02d%02d%02d.TMP", y, mo, d, h, mi, s);
  } else {
    uint32_t run = next_run_number();
    snprintf(buf, len, "/PDL_RUN%04u.TMP", (unsigned)(run % 10000u));
  }
}

PdlHeaderV1 logger_make_default_header() {
  PdlHeaderV1 h{};
  h.magic = PDL_MAGIC;
  h.header_ver = 1;
  h.header_size = sizeof(PdlHeaderV1);
  h.frame_ver = 2;
  h.frame_size = sizeof(PdlFrameV2);
  h.adc_rate_hz = 64000;
  h.frame_rate_hz = 500;
  h.decim = 128;
  h.start_mono_us = (uint64_t)esp_timer_get_time();
  h.slope_mN_per_code = 1.0f;
  h.offset_mN = 0.0f;
  h.accel_g_per_lsb = 1.0f / 16384.0f;
  h.gyro_dps_per_lsb = 1.0f;
  h.overload_mN = 2147483647;   // default: no overload trigger
  h.underload_mN = -2147483648;  // default: no underload trigger
  h.compression_mN = 2147483647; // default: no compression trigger
  h.tare_frames = 0;
  h.tare_adc_code = 0;
  memset(h.reserved, 0, sizeof(h.reserved));
  return h;
}

bool logger_begin() {
  SD_MMC.setPins(4, 5, 6, 7, 8, 9);
  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println("#ERR: SD_MMC.begin failed");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("#ERR: No SD card");
    return false;
  }
  return true;
}

bool logger_is_logging() { return g_logging; }

bool logger_start_session(const PdlHeaderV1& hdr, bool rtc_valid, uint32_t rtc_epoch) {
  if (g_logging) return false;

  make_tmp_filename(rtc_valid, rtc_epoch, g_current_tmp_path, sizeof(g_current_tmp_path));
  g_file = SD_MMC.open(g_current_tmp_path, FILE_WRITE);
  if (!g_file) {
    Serial.println("#ERR: open log file failed");
    return false;
  }

  PdlHeaderV1 h = hdr;
  h.start_rtc_epoch = rtc_valid ? rtc_epoch : 0;
  h.start_rtc_ms = 0;

  size_t n = g_file.write((const uint8_t*)&h, sizeof(h));
  if (n != sizeof(h)) {
    Serial.println("#ERR: header write failed");
    g_file.close();
    return false;
  }
  g_file.flush();
  g_logging = true;
  g_drain_requested = false;
  adc_frames_on_session_start(h);
  Serial.print("#LOGFILE: ");
  Serial.println(g_current_tmp_path);
  return true;
}

void logger_stop_session() {
  if (!g_logging) return;
  g_logging = false;
  g_drain_requested = true;
}

bool logger_has_last_bin() {
  return g_last_bin_path[0] != '\0';
}

bool logger_get_last_bin_path(char* buf, size_t len) {
  if (g_last_bin_path[0] == '\0' || !buf || len == 0) return false;
  strncpy(buf, g_last_bin_path, len - 1);
  buf[len - 1] = '\0';
  return true;
}

static void do_rename_tmp_to_bin() {
  size_t l = strlen(g_current_tmp_path);
  if (l < 5) return;
  char bin_path[64];
  strncpy(bin_path, g_current_tmp_path, sizeof(bin_path) - 1);
  bin_path[sizeof(bin_path)-1] = '\0';
  strcpy(bin_path + l - 3, "BIN");
  if (SD_MMC.exists(bin_path)) SD_MMC.remove(bin_path);
  if (SD_MMC.rename(g_current_tmp_path, bin_path)) {
    strncpy(g_last_bin_path, bin_path, sizeof(g_last_bin_path) - 1);
    g_last_bin_path[sizeof(g_last_bin_path)-1] = '\0';
    Serial.print("#LOGSAVED: ");
    Serial.println(bin_path);
  } else {
    Serial.println("#ERR: rename TMP to BIN failed");
  }
  g_current_tmp_path[0] = '\0';
}

static void logger_task(void*) {
  uint32_t last_flush_ms = millis();
  size_t bytes_since_flush = 0;
  uint8_t* frame_buf = (uint8_t*)malloc(FRAME_BUF_BYTES);
  size_t frame_buf_n = 0;

  if (!frame_buf) {
    Serial.println("#ERR: logger task no buffer");
    vTaskDelete(nullptr);
    return;
  }

  while (true) {
    TareResult tr;
    if (g_tare_result_q && xQueueReceive(g_tare_result_q, &tr, 0) == pdTRUE && g_file) {
      const size_t off_tare = offsetof(PdlHeaderV1, tare_frames);
      g_file.seek(off_tare, SeekSet);
      g_file.write((const uint8_t*)&tr.frames, sizeof(tr.frames));
      g_file.write((const uint8_t*)&tr.adc_code, sizeof(tr.adc_code));
      const size_t off_reserved = offsetof(PdlHeaderV1, reserved);
      g_file.seek(off_reserved, SeekSet);
      g_file.write((const uint8_t*)&tr.duration_ms, sizeof(tr.duration_ms));
      g_file.flush();
    }

    PdlFrameV2 fr;
    BaseType_t received = (g_frame_q && frame_buf_n < FRAME_BUF_COUNT)
      ? xQueueReceive(g_frame_q, &fr, pdMS_TO_TICKS(50))
      : pdFALSE;
    if (received == pdTRUE) {
      if (g_logging && !g_drain_requested) {
        memcpy(frame_buf + frame_buf_n * sizeof(PdlFrameV2), &fr, sizeof(fr));
        frame_buf_n++;
        scope_feed_frame(fr.v1);
      } else if (g_drain_requested && g_file) {
        memcpy(frame_buf + frame_buf_n * sizeof(PdlFrameV2), &fr, sizeof(fr));
        frame_buf_n++;
      }
    }

    if (frame_buf_n >= FRAME_BUF_COUNT && g_file) {
      size_t to_write = frame_buf_n * sizeof(PdlFrameV2);
      size_t n = g_file.write(frame_buf, to_write);
      if (n != to_write) {
        Serial.println("#ERR: SD write failed");
      }
      bytes_since_flush += n;
      frame_buf_n = 0;
    }

    uint32_t now = millis();
    if (g_file && (bytes_since_flush >= FLUSH_INTERVAL_BYTES || (now - last_flush_ms >= 1000))) {
      g_file.flush();
      last_flush_ms = now;
      bytes_since_flush = 0;
    }

    if (g_drain_requested && g_file) {
      while (g_frame_q && xQueueReceive(g_frame_q, &fr, 0) == pdTRUE) {
        memcpy(frame_buf + frame_buf_n * sizeof(PdlFrameV2), &fr, sizeof(fr));
        frame_buf_n++;
        if (frame_buf_n >= FRAME_BUF_COUNT) {
          size_t to_write = frame_buf_n * sizeof(PdlFrameV2);
          g_file.write(frame_buf, to_write);
          frame_buf_n = 0;
        }
      }
      if (frame_buf_n > 0) {
        g_file.write(frame_buf, frame_buf_n * sizeof(PdlFrameV2));
        frame_buf_n = 0;
      }
      g_file.flush();
      g_file.close();
      do_rename_tmp_to_bin();
      g_logging = false;
      g_drain_requested = false;
      Serial.println("#LOGSTOP");
    }
  }
  free(frame_buf);
}

void logger_set_tare_result(uint16_t tare_frames, int32_t tare_adc_code, uint16_t tare_duration_ms) {
  if (!g_tare_result_q) return;
  TareResult tr = { tare_frames, tare_adc_code, tare_duration_ms };
  xQueueOverwrite(g_tare_result_q, &tr);
}

void start_logger_task() {
  g_tare_result_q = xQueueCreate(1, sizeof(TareResult));
  xTaskCreatePinnedToCore(logger_task, "sd_logger", 6144, nullptr, configMAX_PRIORITIES - 4, nullptr, 0);
}

bool logger_export_latest_to_csv() {
  if (g_last_bin_path[0] == '\0') return false;
  File bin = SD_MMC.open(g_last_bin_path, FILE_READ);
  if (!bin) return false;
  size_t l = strlen(g_last_bin_path);
  char csv_path[64];
  strncpy(csv_path, g_last_bin_path, sizeof(csv_path) - 1);
  csv_path[sizeof(csv_path)-1] = '\0';
  if (l >= 4) strcpy(csv_path + l - 4, "CSV");
  File csv = SD_MMC.open(csv_path, FILE_WRITE);
  if (!csv) { bin.close(); return false; }

  PdlHeaderV1 hdr;
  if (bin.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr) || hdr.magic != PDL_MAGIC) {
    bin.close(); csv.close(); return false;
  }

  const uint16_t frame_ver = hdr.frame_ver;
  const size_t frame_size = hdr.frame_size;
  const bool has_imu_ts = (frame_ver >= 2 && frame_size >= sizeof(PdlFrameV2));

  if (has_imu_ts) {
    csv.println("sample_index,t_us,adc_mean,adc_peak,adc_min,force_mean_mN,force_peak_mN,force_min_mN,ax,ay,az,gx,gy,gz,flags,vbat_mV,soc_centiPct,imu_sample_t_us");
  } else {
    csv.println("sample_index,t_us,adc_mean,adc_peak,adc_min,force_mean_mN,force_peak_mN,force_min_mN,ax,ay,az,gx,gy,gz,flags,vbat_mV,soc_centiPct");
  }

  char line[200];
  if (has_imu_ts) {
    PdlFrameV2 fr;
    while (bin.read((uint8_t*)&fr, sizeof(fr)) == sizeof(fr)) {
      const PdlFrameV1& v = fr.v1;
      snprintf(line, sizeof(line), "%u,%llu,%ld,%ld,%ld,%ld,%ld,%ld,%d,%d,%d,%d,%d,%d,%u,%u,%u,%llu",
        (unsigned)v.sample_index, (unsigned long long)v.t_us,
        (long)v.adc_mean, (long)v.adc_peak, (long)v.adc_min,
        (long)v.force_mean_mN, (long)v.force_peak_mN, (long)v.force_min_mN,
        (int)v.ax, (int)v.ay, (int)v.az, (int)v.gx, (int)v.gy, (int)v.gz,
        (unsigned)v.flags, (unsigned)v.vbat_mV, (unsigned)v.soc_centiPct,
        (unsigned long long)fr.imu_sample_t_us);
      csv.println(line);
    }
  } else {
    PdlFrameV1 fr;
    while (bin.read((uint8_t*)&fr, frame_size) == frame_size) {
      snprintf(line, sizeof(line), "%u,%llu,%ld,%ld,%ld,%ld,%ld,%ld,%d,%d,%d,%d,%d,%d,%u,%u,%u",
        (unsigned)fr.sample_index, (unsigned long long)fr.t_us,
        (long)fr.adc_mean, (long)fr.adc_peak, (long)fr.adc_min,
        (long)fr.force_mean_mN, (long)fr.force_peak_mN, (long)fr.force_min_mN,
        (int)fr.ax, (int)fr.ay, (int)fr.az, (int)fr.gx, (int)fr.gy, (int)fr.gz,
        (unsigned)fr.flags, (unsigned)fr.vbat_mV, (unsigned)fr.soc_centiPct);
      csv.println(line);
    }
  }
  csv.flush();
  csv.close();
  bin.close();
  Serial.print("#EXPORT: ");
  Serial.println(csv_path);
  return true;
}
