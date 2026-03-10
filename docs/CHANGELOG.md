# Changelog

## Correctness and low-hanging-fruit fixes (no WiFi/WebUI, binary log format preserved)

### Files changed

| Group | Files |
|-------|--------|
| 1 – Serial CLI | `src/services/serial_cli.cpp` |
| 2 – SD init | `src/main.cpp`, `src/services/ui_state.cpp` |
| 3 – Run number | `src/services/sd_logger.cpp` |
| 4 – Tare | `src/config.h` (new), `src/services/adc_frames.cpp`, `src/services/adc_frames.h`, `src/services/sd_logger.cpp`, `src/services/sd_logger.h` |
| 5 – Per-frame flags | `src/config.h`, `src/services/adc_frames.cpp`, `src/services/sd_logger.cpp` |
| 6 – IMU timestamp | `src/format/log_format.h`, `src/services/aux_state.h`, `src/services/aux_state.cpp`, `src/services/sensors_task.cpp`, `src/services/adc_frames.cpp`, `src/services/sd_logger.cpp` |

### What each fix does

- **Group 1 – Serial CLI:** End-of-line is `\n` (and `\r` is ignored). Spaces are preserved in the line buffer so commands with arguments work (e.g. `scope 25`). CLI remains non-blocking.
- **Group 2 – SD init:** Only `ui_init()` calls `logger_begin()` at boot. `main.cpp` no longer initializes SD; UI state (IDLE_READY vs FAULT + SD fault LED) reflects the single init outcome.
- **Group 3 – Run number:** `/PDL_RUN.NUM` is always exactly 4 bytes: read current value, increment, remove file, then open and write 4 bytes. Prevents append-mode growth and corrupted run numbers.
- **Group 4 – Tare:** One-point tare over the first `TARE_N` frames (default 200) after log start. Mean ADC code is written into the log header (`tare_frames`, `tare_adc_code`, `tare_duration_ms` in reserved). Force uses the computed tare; logging continues during tare.
- **Group 5 – Per-frame flags:** Frames set `FLG_OVERLOAD`, `FLG_UNDERLOAD`, `FLG_COMPRESSION` from header thresholds; `FLG_RTC_INVALID` from aux snapshot; `FLG_LOW_BATT` from config thresholds (SOC and/or voltage). Default header thresholds disable overload/underload/compression until configured.
- **Group 6 – IMU timestamp:** Aux snapshot and frames (PdlFrameV2) include `imu_sample_t_us`. Sensors task records `esp_timer_get_time()` when reading IMU. CSV export adds an `imu_sample_t_us` column for frame_ver ≥ 2; old 56-byte logs still export without it.

### How to test

- **Build:** `pio run` (must complete with no errors).
- **SD:** With no card or bad card, expect FAULT state and SD fault LED; short press retries. With card, expect IDLE_READY after boot.
- **Buttons:** Short press to start log; long press to stop; long press (when stopped) to export latest binary to CSV.
- **Serial CLI:** `help`, `status`, `scope 25` (25 Hz scope), `scope 0` (stop), `startlog`, `stoplog`. `scope 25` must parse `25` as the rate.
- **SD_MMC:** 4-bit mode only: `SD_MMC.setPins(4,5,6,7,8,9)` and `SD_MMC.begin("/sdcard", false)` in `logger_begin()`.
