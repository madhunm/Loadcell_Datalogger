# restartCoreLogger Branch

This document describes the **restartCoreLogger** branch of the Loadcell Datalogger: architecture, pin mapping, button actions, log format, build/usage, and what was changed in this pass.

## What This Branch Is

Restart-friendly parachute/load-cell data logger for ESP32-S3:

- **MAX11270** ADC at 64 ksps, framed to 500 sps and logged to SD as binary `.BIN`.
- **IMU** (LSM6DSV), **RTC** (RX8900CE), **fuel gauge** (MAX17048) on shared I2C; sensors_task runs bring-up and periodic reads.
- **SD_MMC** 4-bit logging; **Serial CLI** at 115200 + scope streaming.
- **LED** (single WS2812) and **button** for start/stop/export/retry; saturated colors only (no pastels).
- **No Wi-Fi / WebUI.**

## Architecture Overview

- **Tasks:** `adc_frames` (core 1, high priority) produces 500 Hz frames; `sd_logger` (core 0) consumes from queue and writes to SD; `sensors_task` (core 0) does IMU/RTC/fuel gauge; `ui_state` driven from `loop()` handles button and LED.
- **Frame flow:** ADC task → `g_frame_q` (PdlFrameV2) → logger task → SD (tmp file, then rename to `.BIN`).
- **Single source of truth for GPIO:** `src/pins.h`.

## Pin Mapping (source: src/pins.h)

| Peripheral    | Pin(s) |
|---------------|--------|
| Log button    | IO 2   |
| NeoPixel      | IO 21  |
| MAX11270 MISO | 12     |
| MAX11270 MOSI | 13     |
| MAX11270 SYNC | 14     |
| MAX11270 RSTB | 15     |
| MAX11270 RDYB | 16     |
| MAX11270 CS   | 17     |
| MAX11270 SCK  | 18     |
| ADC CLK       | Tied to GND → internal clock only |
| SD_MMC CLK    | 4      |
| SD_MMC CMD    | 5      |
| SD_MMC D0–D3  | 6, 7, 8, 9 |
| SD_MMC CD     | 10 (optional) |
| I2C SDA/SCL   | 41, 42 |
| IMU INT1/INT2 | 39, 40 |
| RTC FOUT / INT| 33, 34 |

## Button Actions

- **IDLE_READY / STOPPED:** Short = start log; Long = export latest to CSV; Double = no-op.
- **LOGGING:** Short = set mark flag on next frame; Long = stop log.
- **EXPORTING:** Short = no-op (export runs to completion).
- **FAULT:** Short = retry init (SD mount + sensors probe).

## LED indicators

Single WS2812; saturated colors only. Priority: fault > warning > state.

- **Power on / armed / idle:** Red (pulse during boot, solid when IDLE_READY or after STOPPED double-blink).
- **Logging:** Green heartbeat (1 Hz: 80 ms on, 920 ms off).
- **Low battery:** Orange pulse (2 s period); overrides state when active.
- **Faults:** Red coded blinks (N blinks = fault type, e.g. 2 = SD mount); not solid red so power-on stays distinct.

## Logging Format

- **File:** One 256-byte header (`PdlHeaderV1`) then a stream of 64-byte frames (`PdlFrameV2`).
- **Header:** Magic `0x314C4450`, adc_rate_hz (64000), frame_rate_hz (500), decim (128), start_mono_us, RTC fields, slope/offset/tare, overload/underload/compression, etc.
- **Frame (V2):** sample_index, t_us, adc mean/peak/min, force mean/peak/min (mN), ax/ay/az, gx/gy/gz, flags, vbat_mV, soc_centiPct, imu_sample_t_us.
- **Rate:** 500 frames per second; 128 ADC samples per frame (64 ksps).
- **SD file lifecycle:** Logging writes to a temporary file (`.TMP`); on stop the file is renamed to `.BIN`. Export (long-press after stop) converts the latest `.BIN` to `.CSV` on the SD card.

## Build and Flash

From the repo root:

```bash
pio run
pio run -t upload
```

Default environment is `esp32s3mini` (ESP32-S3, Arduino).

## Scope Streaming and Host Plotter

1. Serial: 115200 baud. Send `scope <hz>` (e.g. `scope 25`); device prints CSV lines. Send `scope 0` to stop.
2. CSV line format: `ms,force_mean_N,force_peak_N,accel_mag_g,flags`. Lines starting with `#` are comments.
3. Host script (Windows):

From repo root:

```cmd
pip install -r tools\host\requirements.txt
python tools\host\scope_plot.py
python tools\host\scope_plot.py --port COM3 --baud 115200 --hz 25 --window 30 --outfile scope_capture.csv
python tools\host\bin2csv.py input.BIN output.csv
```

Omit `--port` to auto-detect. On Ctrl+C the script sends `scope 0` and closes the CSV.

## CSV Export

- **On-device:** After stopping a log, long-press to export latest `.BIN` to a `.CSV` file on the SD card (same base name, extension `.CSV`).
- **Host:** Convert a `.BIN` to CSV on a PC:

```cmd
python tools\host\bin2csv.py input.BIN output.csv
```

Uses header magic and `frame_ver`/`frame_size` to support V1 (56-byte) or V2 (64-byte with `imu_sample_t_us`) frames; columns match the on-device export.

## Changes in This Pass

- **adc_frames.cpp:** 64 ksps to 500 Hz decimation; header slope/offset/tare in `adc_frames_on_session_start()`. Removed C++14/20-only syntax (digit separators, designated initializers) for C++11 portability.
- **sd_logger.cpp:** After writing tare into header reserved, added `g_file.seek(0, SeekEnd)` so frame writes always append. **max11270.h:** Default clock_hz 5000000 (no digit separator).
- **lsm6dsv.cpp:** Verified no uncommented stray text; "Keep BDU=1, IF_INC=1." exists only inside a comment.
- **Host tools:** scope_plot.py, bin2csv.py; requirements in tools/host/requirements.txt. **Repo cleanup:** .gitignore expanded; removed empty root artifact. **Docs:** PLAN updated to pins/LED; CHANGES.md rx8900ce.cpp.
- **sensors_task:** Comment typo fixed (don't). **Guardrails:** .gitattributes, .editorconfig. **Documentation:** This file and root README; PLAN and CHANGES synced. For branch details see root README.
