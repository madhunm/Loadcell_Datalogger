#include "services/sd_logger.h"
#include "config.h"
#include "pins.h"
#include "esp_timer.h"
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <cstddef>
#include <cstring>
#include "freertos/portmacro.h"

#include "services/frame_pipe.h"
#include "services/scope_stream.h"
#include "services/adc_frames.h"
#include "services/system_status.h"

struct TareResult {
  uint16_t frames;
  int32_t adc_code;
  uint16_t duration_ms;
};
static QueueHandle_t g_tare_result_q = nullptr;

static File g_file;
static volatile bool g_logging = false;
static volatile bool g_drain_requested = false;
static volatile bool g_session_write_failed = false;
static volatile bool g_session_opening = false;
static char g_current_tmp_path[64] = "";
static char g_last_bin_path[64] = "";
static portMUX_TYPE g_logger_mux = portMUX_INITIALIZER_UNLOCKED;
static void clear_session_opening() {
  portENTER_CRITICAL(&g_logger_mux);
  g_session_opening = false;
  portEXIT_CRITICAL(&g_logger_mux);
}

static constexpr size_t FRAME_BUF_COUNT = 384;
static constexpr size_t FRAME_BUF_BYTES = FRAME_BUF_COUNT * sizeof(PdlFrameV2);
static constexpr size_t FLUSH_INTERVAL_BYTES = 256 * 1024;
static constexpr const char* RUN_NUM_PATH = "/PDL_RUN.NUM";
static constexpr const char* RUN_NUM_TMP_PATH = "/PDL_RUN.NEW";

static bool recover_run_number_file() {
  if (!SD_MMC.exists(RUN_NUM_TMP_PATH)) return true;

  File f = SD_MMC.open(RUN_NUM_TMP_PATH, FILE_READ);
  if (!f) {
    Serial.println("#ERR: cannot inspect PDL_RUN.NEW");
    return false;
  }
  const bool valid_tmp = f.available() >= 4;
  f.close();
  if (!valid_tmp) {
    Serial.println("#WARN: removing incomplete PDL_RUN.NEW");
    return !SD_MMC.exists(RUN_NUM_TMP_PATH) || SD_MMC.remove(RUN_NUM_TMP_PATH);
  }
  if (SD_MMC.exists(RUN_NUM_PATH) && !SD_MMC.remove(RUN_NUM_PATH)) {
    Serial.println("#ERR: run counter recovery replace failed");
    return false;
  }
  if (SD_MMC.rename(RUN_NUM_TMP_PATH, RUN_NUM_PATH)) {
    Serial.println("#INFO: recovered run counter from PDL_RUN.NEW");
    return true;
  }
  Serial.println("#ERR: run counter recovery failed");
  return false;
}

static void mark_sd_failure_and_finalize() {
  portENTER_CRITICAL(&g_logger_mux);
  g_logging = false;
  g_session_write_failed = true;
  g_drain_requested = true;
  portEXIT_CRITICAL(&g_logger_mux);
}

static bool make_suffixed_path(const char* base_path, const char* ext, int suffix, char* out, size_t out_len) {
  if (!base_path || !ext || !out || out_len == 0) return false;
  const int written = (suffix <= 0)
    ? snprintf(out, out_len, "%s%s", base_path, ext)
    : snprintf(out, out_len, "%s_%02d%s", base_path, suffix, ext);
  return written > 0 && (size_t)written < out_len;
}

static uint32_t next_run_number() {
  uint32_t n = 0;
  File f = SD_MMC.open(RUN_NUM_PATH, FILE_READ);
  if (f && f.available() >= 4) {
    uint8_t b[4];
    f.read(b, 4);
    f.close();
    n = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  } else if (f) {
    f.close();
  }
  n++;
  if (SD_MMC.exists(RUN_NUM_TMP_PATH) && !SD_MMC.remove(RUN_NUM_TMP_PATH)) {
    Serial.println("#ERR: stale run number temp file remove failed");
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    return n;
  }
  f = SD_MMC.open(RUN_NUM_TMP_PATH, FILE_WRITE);
  if (f) {
    uint8_t b[4] = { (uint8_t)(n & 0xFF), (uint8_t)((n >> 8) & 0xFF), (uint8_t)((n >> 16) & 0xFF), (uint8_t)((n >> 24) & 0xFF) };
    size_t written = f.write(b, 4);
    if (written != 4) {
      Serial.println("#ERR: run number file write failed");
      system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    }
    f.flush();
    f.close();
    if (written == 4) {
      if (SD_MMC.exists(RUN_NUM_PATH) && !SD_MMC.remove(RUN_NUM_PATH)) {
        Serial.println("#ERR: run number file replace failed");
        system_status_set_fault(FaultCode::SD_WRITE_FAIL);
      } else if (!SD_MMC.rename(RUN_NUM_TMP_PATH, RUN_NUM_PATH)) {
        Serial.println("#ERR: run number file rename failed");
        system_status_set_fault(FaultCode::SD_WRITE_FAIL);
      }
    }
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

// Frame timestamps (t_us) are monotonic; RTC is used for start_rtc_epoch and filename when valid.
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
  // Defaults match LSM6DSV FsXl::G_16 (0.488 mg/LSB), FsG::DPS_4000 (140 mdps/LSB)
  h.accel_g_per_lsb = 0.488f / 1000.0f;
  h.gyro_dps_per_lsb = 140.0f / 1000.0f;
  h.overload_mN = 2147483647;   // default: no overload trigger
  h.underload_mN = -2147483648;  // default: no underload trigger
  h.compression_mN = 2147483647; // default: no compression trigger
  h.tare_frames = 0;
  h.tare_adc_code = 0;
  memset(h.reserved, 0, sizeof(h.reserved));
  return h;
}

static inline bool sd_card_detect_present() {
  if (!SD_CD_ENABLED) return true;
  int v = digitalRead(PIN_SD_CD);
  return SD_CD_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

bool logger_begin() {
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  if (SD_CD_ENABLED) {
    pinMode(PIN_SD_CD, INPUT_PULLUP);
    if (!sd_card_detect_present()) {
      Serial.println("#ERR: SD card-detect: no card");
      system_status_set_fault(FaultCode::SD_MOUNT_FAIL);
      return false;
    }
  }
  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println("#ERR: SD_MMC.begin failed");
    system_status_set_fault(FaultCode::SD_MOUNT_FAIL);
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("#ERR: No SD card");
    system_status_set_fault(FaultCode::SD_MOUNT_FAIL);
    return false;
  }
  if (!recover_run_number_file()) {
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
  }
  system_status_clear_fault(FaultCode::SD_MOUNT_FAIL);
  return true;
}

bool logger_is_logging() {
  portENTER_CRITICAL(&g_logger_mux);
  const bool logging = g_logging;
  portEXIT_CRITICAL(&g_logger_mux);
  return logging;
}

bool logger_is_busy() {
  portENTER_CRITICAL(&g_logger_mux);
  const bool busy = g_logging || g_drain_requested || g_session_opening;
  portEXIT_CRITICAL(&g_logger_mux);
  return busy;
}

bool logger_can_start() {
  return !logger_is_busy();
}

bool logger_start_session(const PdlHeaderV1& hdr, bool rtc_valid, uint32_t rtc_epoch) {
  portENTER_CRITICAL(&g_logger_mux);
  if (g_logging || g_drain_requested || g_session_opening) {
    portEXIT_CRITICAL(&g_logger_mux);
    return false;
  }
  g_session_opening = true;
  portEXIT_CRITICAL(&g_logger_mux);

  char base_tmp_path[64];
  make_tmp_filename(rtc_valid, rtc_epoch, g_current_tmp_path, sizeof(g_current_tmp_path));
  strncpy(base_tmp_path, g_current_tmp_path, sizeof(base_tmp_path) - 1);
  base_tmp_path[sizeof(base_tmp_path) - 1] = '\0';
  const size_t base_len = strlen(base_tmp_path);
  if (base_len < 4 || strcmp(base_tmp_path + base_len - 4, ".TMP") != 0) {
    clear_session_opening();
    return false;
  }
  base_tmp_path[base_len - 4] = '\0';
  for (int i = 0; i < 100; ++i) {
    if (!make_suffixed_path(base_tmp_path, ".TMP", i, g_current_tmp_path, sizeof(g_current_tmp_path))) {
      clear_session_opening();
      return false;
    }
    if (!SD_MMC.exists(g_current_tmp_path)) break;
  }
  if (SD_MMC.exists(g_current_tmp_path)) {
    Serial.println("#ERR: no available TMP filename");
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    clear_session_opening();
    return false;
  }
  g_file = SD_MMC.open(g_current_tmp_path, FILE_WRITE);
  if (!g_file) {
    Serial.println("#ERR: open log file failed");
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    clear_session_opening();
    return false;
  }

  PdlHeaderV1 h = hdr;
  h.start_mono_us = (uint64_t)esp_timer_get_time();
  h.start_rtc_epoch = rtc_valid ? rtc_epoch : 0;
  h.start_rtc_ms = 0;

  size_t n = g_file.write((const uint8_t*)&h, sizeof(h));
  if (n != sizeof(h)) {
    Serial.println("#ERR: header write failed");
    g_file.close();
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    clear_session_opening();
    return false;
  }
  g_file.flush();
  adc_frames_on_session_start(h);
  portENTER_CRITICAL(&g_logger_mux);
  g_logging = true;
  g_drain_requested = false;
  g_session_write_failed = false;
  g_session_opening = false;
  portEXIT_CRITICAL(&g_logger_mux);
  Serial.print("#LOGFILE: ");
  Serial.println(g_current_tmp_path);
  return true;
}

void logger_stop_session() {
  portENTER_CRITICAL(&g_logger_mux);
  if (g_session_opening || g_drain_requested || !g_logging) {
    portEXIT_CRITICAL(&g_logger_mux);
    return;
  }
  g_logging = false;
  g_drain_requested = true;
  portEXIT_CRITICAL(&g_logger_mux);
  Serial.println("#LOGSTOP: finalizing");
}

bool logger_has_last_bin() {
  portENTER_CRITICAL(&g_logger_mux);
  const bool has_last_bin = g_last_bin_path[0] != '\0';
  portEXIT_CRITICAL(&g_logger_mux);
  return has_last_bin;
}

bool logger_get_last_bin_path(char* buf, size_t len) {
  if (g_last_bin_path[0] == '\0' || !buf || len == 0) return false;
  portENTER_CRITICAL(&g_logger_mux);
  strncpy(buf, g_last_bin_path, len - 1);
  buf[len - 1] = '\0';
  portEXIT_CRITICAL(&g_logger_mux);
  return true;
}

static void do_rename_tmp_to_bin() {
  size_t l = strlen(g_current_tmp_path);
  if (l < 5) return;
  char base_bin_path[64];
  char bin_path[64];
  strncpy(base_bin_path, g_current_tmp_path, sizeof(base_bin_path) - 1);
  base_bin_path[sizeof(base_bin_path)-1] = '\0';
  base_bin_path[l - 4] = '\0';
  for (int i = 0; i < 100; ++i) {
    if (!make_suffixed_path(base_bin_path, ".BIN", i, bin_path, sizeof(bin_path))) {
      Serial.println("#ERR: BIN filename generation failed");
      system_status_set_fault(FaultCode::SD_WRITE_FAIL);
      return;
    }
    if (!SD_MMC.exists(bin_path)) break;
  }
  if (SD_MMC.exists(bin_path)) {
    Serial.println("#ERR: no available BIN filename");
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    return;
  }
  if (SD_MMC.rename(g_current_tmp_path, bin_path)) {
    strncpy(g_last_bin_path, bin_path, sizeof(g_last_bin_path) - 1);
    g_last_bin_path[sizeof(g_last_bin_path)-1] = '\0';
    Serial.print("#LOGSAVED: ");
    Serial.println(bin_path);
    g_current_tmp_path[0] = '\0';
  } else {
    Serial.println("#ERR: rename TMP to BIN failed");
    system_status_set_fault(FaultCode::SD_WRITE_FAIL);
    /* leave g_current_tmp_path set so .TMP remains identifiable for recovery */
  }
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
    if (SD_CD_ENABLED && g_logging && !g_drain_requested && !sd_card_detect_present()) {
      system_status_set_fault(FaultCode::SD_WRITE_FAIL);
      mark_sd_failure_and_finalize();
    }

    TareResult tr;
    if (g_tare_result_q && xQueueReceive(g_tare_result_q, &tr, 0) == pdTRUE && g_file && !g_session_write_failed) {
      const size_t off_tare = offsetof(PdlHeaderV1, tare_frames);
      bool tare_ok = g_file.seek(off_tare, SeekSet);
      if (tare_ok) tare_ok = (g_file.write((const uint8_t*)&tr.frames, sizeof(tr.frames)) == sizeof(tr.frames));
      if (tare_ok) tare_ok = (g_file.write((const uint8_t*)&tr.adc_code, sizeof(tr.adc_code)) == sizeof(tr.adc_code));
      const size_t off_reserved = offsetof(PdlHeaderV1, reserved);
      if (tare_ok) tare_ok = g_file.seek(off_reserved, SeekSet);
      if (tare_ok) tare_ok = (g_file.write((const uint8_t*)&tr.duration_ms, sizeof(tr.duration_ms)) == sizeof(tr.duration_ms));
      if (tare_ok) {
        g_file.flush();
        g_file.seek(0, SeekEnd);
      } else {
        Serial.println("#ERR: tare header patch failed");
        system_status_set_fault(FaultCode::SD_WRITE_FAIL);
        mark_sd_failure_and_finalize();
      }
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
        system_status_set_fault(FaultCode::SD_WRITE_FAIL);
        mark_sd_failure_and_finalize();
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
      Serial.println("#LOGSTOP: drain/close in progress");
      bool drain_ok = true;
      while (g_frame_q && xQueueReceive(g_frame_q, &fr, 0) == pdTRUE) {
        memcpy(frame_buf + frame_buf_n * sizeof(PdlFrameV2), &fr, sizeof(fr));
        frame_buf_n++;
        if (frame_buf_n >= FRAME_BUF_COUNT) {
          size_t to_write = frame_buf_n * sizeof(PdlFrameV2);
          size_t n = g_file.write(frame_buf, to_write);
          if (n != to_write) {
            drain_ok = false;
            g_session_write_failed = true;
            system_status_set_fault(FaultCode::SD_WRITE_FAIL);
          }
          frame_buf_n = 0;
        }
      }
      if (frame_buf_n > 0 && drain_ok) {
        size_t to_write = frame_buf_n * sizeof(PdlFrameV2);
        size_t n = g_file.write(frame_buf, to_write);
        if (n != to_write) {
          drain_ok = false;
          g_session_write_failed = true;
          system_status_set_fault(FaultCode::SD_WRITE_FAIL);
        }
        frame_buf_n = 0;
      }
      g_file.flush();
      g_file.close();
      if (g_session_write_failed) {
        Serial.println("#ERR: finalize failed (TMP left for recovery)");
        Serial.print("# recovery TMP: ");
        Serial.println(g_current_tmp_path);
        /* keep g_current_tmp_path so recovery context remains until next successful start */
      } else {
        do_rename_tmp_to_bin();
        Serial.println("#LOGSTOP: complete");
      }
      portENTER_CRITICAL(&g_logger_mux);
      g_logging = false;
      g_drain_requested = false;
      portEXIT_CRITICAL(&g_logger_mux);
    }
  }
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
  if (logger_is_busy()) {
    Serial.println("#ERR: logger busy (finalizing previous session)");
    return false;
  }
  if (g_last_bin_path[0] == '\0') return false;
  File bin = SD_MMC.open(g_last_bin_path, FILE_READ);
  if (!bin) { system_status_set_fault(FaultCode::SD_WRITE_FAIL); return false; }
  size_t l = strlen(g_last_bin_path);
  char csv_path[64];
  strncpy(csv_path, g_last_bin_path, sizeof(csv_path) - 1);
  csv_path[sizeof(csv_path) - 1] = '\0';
  char* last_dot = strrchr(csv_path, '.');
  if (last_dot && (size_t)(last_dot - csv_path) + 5 <= sizeof(csv_path))
    strcpy(last_dot, ".CSV");
  else if (l + 4 < sizeof(csv_path))
    strcat(csv_path, ".CSV");
  if (SD_MMC.exists(csv_path)) {
    if (!SD_MMC.remove(csv_path)) {
      Serial.println("#ERR: cannot remove existing CSV for export");
      bin.close();
      system_status_set_fault(FaultCode::SD_WRITE_FAIL);
      return false;
    }
  }
  File csv = SD_MMC.open(csv_path, FILE_WRITE);
  if (!csv) { bin.close(); system_status_set_fault(FaultCode::SD_WRITE_FAIL); return false; }

  PdlHeaderV1 hdr;
  if (bin.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr) || hdr.magic != PDL_MAGIC) {
    bin.close(); csv.close(); system_status_set_fault(FaultCode::SD_WRITE_FAIL); return false;
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
