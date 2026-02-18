# PDL Host Tools

Python scripts for Windows (and other platforms) to work with the Loadcell Datalogger firmware.

## Requirements

```cmd
pip install -r requirements.txt
```

Or manually:

```cmd
pip install pyserial matplotlib numpy
```

## bin2csv.py

Converts a PDL binary log (`.BIN`) to CSV. Matches the on-device export format.

**Usage (Windows):**

```cmd
python tools\host\bin2csv.py LOG0001.BIN LOG0001.csv
```

Or from the repo root with full paths:

```cmd
python tools\host\bin2csv.py C:\path\to\PDL_RUN0001.BIN C:\path\to\output.csv
```

- Validates magic `0x314C4450` and uses `frame_ver` / `frame_size` from the header.
- If `frame_ver >= 2` and `frame_size >= 64`, outputs V2 frames including `imu_sample_t_us`.
- Otherwise parses V1 frames (56 bytes).

## scope_plot.py

Live scope: sends `scope <hz>` to the device over serial, parses CSV lines, and live-plots force and accel.

**Usage (Windows):**

Auto-detect port, default 115200 baud, 25 Hz scope rate:

```cmd
python tools\host\scope_plot.py
```

Specify port and baud:

```cmd
python tools\host\scope_plot.py --port COM3 --baud 115200
```

All options:

```cmd
python tools\host\scope_plot.py --port COM3 --baud 115200 --hz 25 --window 30 --outfile scope_capture.csv
```

- `--port`: Serial port (e.g. `COM3`). Omit to auto-select.
- `--baud`: Baud rate (default 115200).
- `--hz`: Scope rate in Hz sent to device (default 25).
- `--window`: Plot window in seconds (default 30).
- `--outfile`: Output CSV path (default `scope_capture.csv`).

CSV line format from device: `ms,force_mean_N,force_peak_N,accel_mag_g,flags`.

Press Ctrl+C to stop; script sends `scope 0` and closes the CSV file.
