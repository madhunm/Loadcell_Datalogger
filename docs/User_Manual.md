# Parachute Load-Cell Data Logger — User Manual

This manual describes how to operate the parachute tension data logger in the field. It is written for operators who deploy the device and retrieve data. No programming or firmware knowledge is required.

**Printing (A4):** For best results, print on A4 paper. Use your viewer’s “Print” or “Print to PDF” and set paper size to **A4** and margins to **Normal** or **Default**. Each major section is set to start on a new page so the booklet stays readable.

---

## 1. Overview

### What the device does

The logger is a battery-powered, handheld unit that records **load-cell (tension) data** and **motion data** during parachute deployment and descent. It writes continuously to a removable SD card. You start and stop recording with a single button and can export the latest recording to a spreadsheet-ready file on the same card.

### What it records

- **Tension (force)** from the load cell at high speed, summarized every 2 milliseconds into average, peak, and minimum force for that interval.
- **Motion** from an onboard inertial sensor (accelerometer and gyroscope), time-aligned with the tension data.
- **Battery voltage and charge level** for each time interval.
- **Event marks** that you can trigger with a button press during logging (e.g., to mark “deployment” or “landing”).

All data is stored with timestamps so you can analyze tension and motion together after the flight.

### Intended use

The device is designed for **parachute tension logging**: capturing force on the risers or lines and body motion during deployment and descent. Data is intended for post-flight analysis on a computer (e.g., spreadsheets or analysis tools). The unit does not display data in the field; status is shown only by the status LED and by the files on the SD card.

---

<div style="page-break-before: always"></div>

## 2. Device Components

| Component | Description |
|-----------|-------------|
| **Button** | Single control button on the front. Used to start/stop logging, mark events, export data, and retry after a fault. |
| **Status LED** | Single RGB LED (next to or near the button). Shows power state, logging state, low battery, faults, and export progress. Colors and patterns are fixed (see Section 3). |
| **SD card slot** | Holds a standard SD card. All recordings are saved here. The card must be inserted before power-on for normal operation. |
| **USB port** | Used for power (and, optionally, for advanced users: serial connection to a computer). For normal field use, connect only for charging or power. |

---

<div style="page-break-before: always"></div>

## 3. LED Status Guide

Use this table to interpret the status LED. **Saturated colors only** (no dim or pastel); if the LED is off, the device has no power or has failed to start.

| Color | Pattern | Meaning | User action |
|-------|---------|---------|-------------|
| Red | Solid | Power on / Idle. Device is ready. | Short press to start logging; long press to export latest log to CSV. |
| Green | Heartbeat (brief flash about once per second) | Logging in progress. | Short press to mark an event; long press to stop logging. |
| Yellow | Blink while busy | Finalizing after stop request (draining/closing the log safely). | Wait; do not remove the SD card or try to start/export yet. |
| Green | Double-blink then off | Just stopped logging. | Wait; LED will soon go solid red (idle). |
| Red | Solid | Idle again after double-blink. | Same as “Power on / Idle” above. |
| Orange | Slow pulse (about 2 seconds per cycle) | Low battery warning. | Recharge soon. Logging can continue but may stop if battery fails. |
| Orange | Double-blink | Battery telemetry unavailable (fuel-gauge communication fault). | Logging can continue, but battery readings are unavailable until recovery. |
| Purple | Slow pulse | Export in progress (converting latest log to CSV). | Wait until LED returns to solid red. Do not remove the SD card. |
| Red | Coded blinks (e.g., 2 or 3 blinks, pause, repeat) | Fault condition. | Short press to retry (e.g., re-check SD card, sensors). If retry fails, power off and fix the issue (e.g., insert SD card), then power on again. |

**Important:** A **solid red** LED means “power on / idle.” **Red blinking** means a fault (e.g., SD card missing, write error). Do not confuse the two.

---

<div style="page-break-before: always"></div>

## 4. Basic Operation

### A. Powering on

1. Insert the SD card fully into the slot (if not already inserted).
2. Connect power (battery or USB).
3. Wait a few seconds. The LED will show a **red pulse** during startup, then turn **solid red** when the device is ready. If it stays red blinking, see Section 7 (Fault Conditions).

### B. Starting a logging session

1. Ensure the LED is **solid red** (idle).
2. **Short press** the button. The LED should change to a **green heartbeat** (logging).
3. You can now deploy; the device is recording tension and motion to the SD card.

### C. During descent

- The LED stays **green heartbeat** while logging.
- **Short press** at any time to place an **event mark** in the data (e.g., deployment, line stretch, landing). No need to stop logging.
- If the LED turns **orange** (low battery), logging continues but you should land and recharge as soon as practical.
- If the LED turns **red blinking**, a fault has occurred; see Section 7.

### D. Stopping logging after landing

1. With the LED still in **green heartbeat**, **long press** the button.
2. The LED will first blink **yellow** while the logger finalizes the file, then show a **green double-blink**, then go **solid red**. Logging has stopped and the file has been closed and renamed on the SD card.

### E. Exporting data

Export converts the **latest** binary log file on the SD card into a **CSV file** (spreadsheet-ready) on the same card.

1. Ensure the LED is **solid red** (idle) and that you have at least one completed log (you have stopped logging at least once).
2. **Long press** the button. The LED will show a **purple pulse** while the export runs.
3. When the LED returns to **solid red**, export is complete. The CSV file is on the SD card with the same base name as the log, but with a `.CSV` extension.

### F. Removing the SD card safely

1. Make sure the LED is **solid red** and **not** showing a purple pulse (no export in progress).
2. Do not press the button to start or stop logging while removing the card.
3. Power off the device, or wait until no file activity is expected, then remove the SD card.

---

<div style="page-break-before: always"></div>

## 5. Data Files

### File types

- **.BIN** — Raw binary log file. Contains header (session info, calibration, timestamps) followed by many frames of tension, motion, and battery data. Not human-readable; use the device’s export or a host conversion tool to get CSV.
- **.CSV** — Exported spreadsheet-ready file. One row per time step; columns include time, force (mean/peak/min), motion (accelerometer/gyro), battery, and flags. Open in Excel, Google Sheets, or similar.

### File naming

- If the real-time clock is set and valid at the start of a log, the file name uses date and time: **PDL_YYYYMMDD_HHMMSS** (e.g., `PDL_20250118_143022.BIN` and `PDL_20250118_143022.CSV`).
- If the clock is not valid, the device uses a run number: **PDL_RUN####** (e.g., `PDL_RUN0001.BIN` and `PDL_RUN0001.CSV`).
- To avoid overwriting existing logs, the device may add a suffix to either type of name when a file already exists (e.g. `PDL_20250118_143022_01.BIN` or `PDL_RUN0001_01.BIN`). If the run-number file cannot be updated (e.g. card full), the device still assigns a run-based name and uses a suffix when needed so existing files are not overwritten.

The **latest** log (most recently stopped) is the one that gets exported when you long-press from idle.

### RTC and time in logs

The real-time clock (RTC) is used only for **session start**: the filename (when valid) and the header field `start_rtc_epoch`. Each frame’s time is a **monotonic timestamp** (`t_us`), not wall-clock. If the RTC is invalid or not set at session start, filenames use a run number (e.g. `PDL_RUN0001`) and the header marks RTC as invalid. Serial CLI command `startlog` also uses the RTC when valid (same as button start).

### CSV columns (summary)

Typical columns you will see after export (exact names may vary slightly):

- **Time** — Timestamp of the sample (microseconds or milliseconds from start).
- **Force (mean, peak, min)** — Tension over the 2 ms window (e.g., in millinewtons or Newtons, depending on calibration).
- **Accelerometer / Gyroscope** — Motion axes; time-aligned with the force data.
- **Battery** — Voltage and state-of-charge for that sample.
- **Flags** — Indicators for overload, underload, low battery, event mark, dropped samples, etc. Your analysis software or spreadsheet can filter on these.

---

<div style="page-break-before: always"></div>

## 6. Battery Behavior

- **Orange pulse** — Low battery warning. The device is still running and can keep logging, but you should recharge as soon as possible.
- **Orange double-blink** — Battery telemetry is temporarily unavailable because the fuel gauge is not responding. Logging can continue, but the battery fields in the log are invalid until communication recovers.
- If the battery fails during logging, the device may stop writing and the LED may show a fault (red blink). Data already written to the SD card up to that point remains valid; the last few seconds might be missing or incomplete.
- Recharge the unit according to your hardware instructions. Do not remove the SD card during charging if you want to avoid any risk of file system damage.

---

<div style="page-break-before: always"></div>

## 7. Fault Conditions

| Situation | LED | What to do |
|-----------|-----|------------|
| SD card missing or not detected | Red blink (e.g., 2 blinks, pause, repeat) | Power off, insert SD card fully, power on. When idle (solid red), short press to retry if needed. |
| SD card write error (e.g., card full or faulty) | Red blink (e.g., 3 blinks) | Short press to retry. If it continues, power off, replace or free space on the SD card, then power on. |
| Sensor or internal fault | Red blink (other patterns) | Short press to retry initialization. If the fault persists, power off and power on. If it still fails, the unit may need service. |

**When to power cycle:** If short-press retry does not clear the fault and the LED keeps blinking red, power off, correct the cause (e.g., insert card, replace card), then power on again.

---

<div style="page-break-before: always"></div>

## 8. Best Practices Before Flight

1. **Insert the SD card** and ensure it is fully seated.
2. **Power on** and wait until the LED is **solid red** (idle). If you see red blinking, resolve the fault before flight.
3. **Perform a short test:** Short press to start (green heartbeat), wait a few seconds, long press to stop (yellow finalize blink, then green double-blink, then solid red).
4. **Confirm a file was created:** After the test, power off, remove the SD card, and check on a computer that a new .BIN (and optionally .CSV) file is present. Reinsert the card and power on for the actual flight.
5. **Charge the battery** so the LED does not show an orange pulse at takeoff.

---

<div style="page-break-before: always"></div>

## 9. Troubleshooting

| Problem | Possible cause | Action |
|---------|----------------|--------|
| No LED at all | No power, or unit not starting | Check battery or USB connection; try a different cable or power source. |
| Red blinking and never solid red | SD card missing, faulty, or full; or sensor fault | Insert or replace SD card; free space if full; short press to retry; power cycle if needed. |
| Long press from idle does not create CSV | No previous log on card, or export failed | Ensure you have stopped a log at least once and waited for stop finalization to complete (yellow blink, then green double-blink, then red). Try export again; if orange or red blink appears, check SD card and retry. |
| File missing or unreadable on PC | Card removed during write, or card corrupted | Always stop logging (long press) and wait for solid red before removing the card. Use a known-good SD card and avoid removing it during purple pulse (export). |

---

<div style="page-break-before: always"></div>

## 10. Technical Specifications (User-Level)

| Item | Specification |
|------|---------------|
| Load-cell sampling | High-speed acquisition (e.g., 64,000 samples per second internally). |
| Logging rate | 500 frames per second (one frame every 2 ms) to the SD card. |
| Time alignment | Tension and motion (IMU) data are time-aligned in each frame; timestamps are stored for analysis. |
| Storage format | Binary (.BIN) with a 256-byte header and fixed-size frames; export produces CSV. |
| Operating voltage | Depends on your battery pack; the unit is designed for typical single-cell or multi-cell Li-ion operation. See your hardware documentation. |

---

<div style="page-break-before: always"></div>

## 11. Revision

| Version | Firmware branch | Date |
|---------|-----------------|------|
| 1.0 | restartCoreLogger | 2025 |

---

*For developer and build information, see the project README and branch documentation. This manual is for end-user operation only.*
