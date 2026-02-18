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

## Logging Format

- **File:** One 256-byte header (`PdlHeaderV1`) then a stream of 64-byte frames (`PdlFrameV2`).
- **Header:** Magic `0x314C4450`, adc_rate_hz (64000), frame_rate_hz (500), decim (128), start_mono_us, RTC fields, slope/offset/tare, overload/underload/compression, etc.
- **Frame (V2):** sample_index, t_us, adc mean/peak/min, force mean/peak/min (mN), ax/ay/az, gx/gy/gz, flags, vbat_mV, soc_centiPct, imu_sample_t_us.
- **Rate:** 500 frames per second; 128 ADC samples per frame (64 ksps).

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

```cmd
pip install pyserial matplotlib numpy
python tools\host\scope_plot.py --port COM3 --baud 115200 --hz 25 --window 30 --outfile scope.csv
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

- **adc_frames.cpp:** `adc_frames_on_session_start()` now copies header `slope_mN_per_code` and `offset_mN` into the calibration used for force conversion, and applies header `tare_adc_code` / `tare_frames` so existing tare is used when present (avoids re-tare when header already has tare).
- **Includes:** Searched for malformed `#include` lines in `src/`; none found.
- **lsm6dsv.cpp:** Verified no uncommented stray text; "Keep BDU=1, IF_INC=1." exists only inside a comment.
- **Host tools:** Confirmed `scope_plot.py` and `bin2csv.py` compile with `python -m py_compile`; requirements (pyserial, matplotlib, numpy) documented in `tools/host/requirements.txt`.
- **Documentation:** This file added; root `README.md` updated to link here and to correct the “IMU and fuel gauge stubbed” statement for this branch.
