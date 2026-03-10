# Button + LED UI + SD Logger — Implementation Plan

**Authoritative branch doc:** See [docs/restartCoreLogger/README.md](restartCoreLogger/README.md) for current pin map, LED behavior, and usage. This plan is kept in sync with the implementation.

## Implementation summary (file changes)

- **src/format/pdl_flags.h** — Added `FLG_MARK` for MARK_EVENT (next-frame flag).
- **src/services/button.h, button.cpp** — New: debounce 25 ms, short/long/double gestures, event queue; `button_begin(pin, active_low)`, `button_tick(now_ms)`, `button_get_event(&ev)`.
- **src/services/led_ui.h, led_ui.cpp** — New: single WS2812 (Adafruit NeoPixel), saturated colors, SOLID/BLINK/N_BLINK/PULSE/DOUBLE_BLINK; state/warning/fault patterns; `led_begin(pin)`, `led_tick(now_ms)`, `led_set_state` / `led_set_fault` / `led_set_warning`.
- **src/services/sd_logger.h, sd_logger.cpp** — 4-bit SD_MMC (`setPins(4,5,6,7,8,9)`, `begin("/sdcard", false)`); filename `PDL_YYYYMMDD_HHMMSS.TMP` (RTC) or `PDL_RUN####.TMP`; drain then rename to `.BIN`; block writes (256 frames); flush every 1 s or 256 KB; `logger_start_session(hdr, rtc_valid, rtc_epoch)`, `logger_has_last_bin()`, `logger_get_last_bin_path()`, `logger_export_latest_to_csv()`.
- **src/services/frame_pipe.h, frame_pipe.cpp** — `frame_pipe_set_mark_next()`, `frame_pipe_consume_mark_next()`, `frame_pipe_notify_drop(now_ms)`, `frame_pipe_should_set_dropped(now_ms)` for FLG_MARK and sticky FLG_DROPPED_FRAME.
- **src/services/adc_frames.cpp** — Uses `aux_get_snapshot()` for frame IMU/batt; applies FLG_MARK and FLG_DROPPED_FRAME; on queue full calls `frame_pipe_notify_drop()`.
- **src/services/ui_state.h, ui_state.cpp** — New: UI state machine (BOOT/IDLE_READY/LOGGING/STOPPED/EXPORTING/FAULT); gesture mapping; `ui_init()`, `ui_tick(now_ms)`; **button GPIO 2 (IO2), NeoPixel LED GPIO 21 (IO21)** — see `src/pins.h`.
- **src/main.cpp** — Calls `ui_init()` after logger/adc/sensors, `ui_tick(millis())` in loop with 10 ms delay; `start_sensors_task()`.
- **src/services/serial_cli.cpp** — `logger_start_session(hdr, false, 0)` for CLI startlog.
- **src/services/sensors_task.cpp** — Fixed driver API: object declarations (no parentheses), `begin(Wire)`, `imu.configure(Odr::HZ_960, Odr::HZ_960)`, `readRaw(SampleRaw)`, `readVoltage_mV`/`readSOC_centiPercent`, `readDateTime(DateTime)`.
- **platformio.ini** — `lib_deps = adafruit/Adafruit NeoPixel`.

---

## Overview

Add single-button gesture UI and WS2812 LED state/warning/fault patterns, and complete the SD binary logger with .TMP→.BIN lifecycle, buffered non-blocking writes, and on-board BIN→CSV export. No WiFi/WebUI; primary log stays binary; 64 ksps acquisition must not block on SD.

---

## A) Button state machine and gestures

### New files
- **src/services/button.h** — Button API: gesture enum, event type, poll/queue API.
- **src/services/button.cpp** — Debounce + gesture detection, event queue.

### Gesture timing (lock in)
- **Debounce**: 25 ms.
- **Short press**: press duration 50–350 ms.
- **Long press**: press duration ≥ 1500 ms.
- **Double press**: two short presses with gap between releases ≤ 350 ms.

### Event types (enum)
- `SHORT_PRESS`, `LONG_PRESS`, `DOUBLE_PRESS` (and optionally `NO_EVENT` for polling).

### Implementation approach
- **Input**: One GPIO (e.g. configurable in button.cpp or via `begin(pin)`); active-low or active-high configurable.
- **Debounce**: State machine with `last_stable_state`, `last_change_ms`; only emit “pressed”/“released” after 25 ms stable.
- **Gesture state machine**: Track press start time; on release classify as short/long; for double, track time since last release and second press.
- **Output**: Small FreeRTOS queue (e.g. 4 elements) of gesture events, or single “last event” + flag. Main loop or UI task calls `button_poll()` (or `button_tick(millis())`) and drains events. No blocking waits inside button module; ISR can only set a “pin changed” flag and timestamp; poll in task for debounce/gesture logic (simpler and robust for gloves).

### API (minimal)
- `void button_begin(int gpio_pin, bool active_low = true);`
- `void button_tick(uint32_t now_ms);`  // call from loop or a task every few ms
- `bool button_get_event(ButtonEvent* out);`  // returns true if event popped and filled
- Optional: `void button_set_callback(void (*cb)(ButtonEvent));` for callback style.

---

## B) WS2812 LED patterns (saturated only)

### New files
- **src/services/led_ui.h** — Pattern types, colors, state/warning/fault APIs, `led_tick(millis())`.
- **src/services/led_ui.cpp** — Single WS2812 driver (e.g. RMT or bit-bang), pattern state machine, no `delay()`.

### Hardware
- One WS2812 on **GPIO 21 (IO21)** — see `src/pins.h` (PIN_NEOPIXEL). Use ESP32 RMT or Adafruit NeoPixel. **Customer mandates:** saturated colors only (no pastels); **power/idle = RED**, **logging = GREEN**, **low battery = ORANGE**; faults = RED coded blinks (not solid red so power-on stays distinct).

### Colors (saturated)
- RED, GREEN, BLUE, CYAN, YELLOW, MAGENTA, PURPLE, ORANGE (fixed saturated RGB). Default brightness: 96/255; only brightness varies for PULSE, hue stays saturated.

### Pattern primitives
- **SOLID(color)** — Steady color.
- **BLINK(color, on_ms, off_ms)** — Repeat on/off.
- **DOUBLE_BLINK(color, on, gap, on, long_off)** — Two blinks then long off (then optionally go to SOLID for STOPPED).
- **N_BLINK(color, n, on, off, long_off)** — n blinks, then long off, repeat.
- **PULSE(color, period_ms)** — Brightness modulation (e.g. sine or ramp), period given.

### Priority
- **FAULT** > **WARNING** > **STATE**. When a fault is active, show fault pattern; else warning; else state pattern.

### State patterns (exact — matches src/services/led_ui.cpp)
- **BOOT**: PULSE(RED, 1200) — power-on indicator.
- **IDLE_READY**: SOLID(RED) — armed / power on.
- **LOGGING**: BLINK(GREEN, 80, 920) — 1 Hz heartbeat.
- **STOPPED**: DOUBLE_BLINK(GREEN, 80, 120, 80, 1200) then SOLID(RED).
- **EXPORTING**: PULSE(PURPLE, 1000).
- **FAULT**: RED N_BLINK codes (per-fault below); never SOLID(RED) for faults.

### Warnings (overlay; do not hide LOGGING heartbeat when logging)
- RTC invalid: BLINK(YELLOW, 40, 1960) — yellow tick every 2 s.
- Battery low: **PULSE(ORANGE, 2000)** — customer mandate: low battery = ORANGE.
- Underload/slack: BLINK(BLUE, 120, 380) while true.
- Overload approaching: BLINK(YELLOW, 120, 120) while true.
- Compression: BLINK(MAGENTA, 120, 380) while true.

### Faults (repeat forever; highest priority)
- SD missing/mount fail: N_BLINK(RED, 2, 120, 120, 1200).
- SD write error during logging: N_BLINK(RED, 3, 120, 120, 1200).
- ADC fault (RDY stuck / SPI failure): N_BLINK(RED, 4, 120, 120, 1200).
- IMU fault (if IMU mandatory): N_BLINK(RED, 5, 120, 120, 1200); else warning: N_BLINK(MAGENTA, 2, …).
- RTC fault: treat as warning (e.g. yellow) unless explicitly required as fault.

### Reserved
- **PATTERN_CUSTOM1**, **PATTERN_CUSTOM2** — Placeholder APIs or enums for future mandated patterns.

### Implementation
- **Non-blocking**: `led_tick(uint32_t now_ms)` advances pattern state; call from loop or a dedicated low-priority task every ~10–20 ms.
- **No delay()** inside pattern engine; use timers only.
- **Single LED**: One pixel; keep buffer size minimal (e.g. 3 bytes RGB).

---

## C) SD card logger (binary) — full implementation

### File lifecycle
- **START_LOG**:
  - Ensure SD mounted (`logger_begin()` or remount if coming from FAULT retry); verify `SD_MMC.cardType() != CARD_NONE`.
  - Filename:
    - If RTC valid: `PDL_YYYYMMDD_HHMMSS.TMP` (e.g. from `aux_get_snapshot().rtc_epoch` or RTC driver).
    - Else: `PDL_RUN####.TMP` (increment run number, e.g. from NVS or a small file).
  - Open file for write, write versioned binary header (existing `PdlHeaderV1`), then start enqueueing frames.
- **STOP_LOG**:
  - Signal “drain and stop” to writer task.
  - Writer drains queue, flush(), close(), then rename `.TMP` → `.BIN` (use `SD_MMC.rename(old, new)` or equivalent).

### SD_MMC 4-bit (mandatory)
- `SD_MMC.setPins(4, 5, 6, 7, 8, 9)` before first `begin` — pins from `src/pins.h` (CLK 4, CMD 5, D0–D3 6–9).
- `SD_MMC.begin("/sdcard", false)` (mode1bit = false).
- **Optional SD card-detect pin IO10** — gated by `config.h` (e.g. SD_CD_ENABLED); if enabled, fail mount when card absent and stop logging safely if card removed. Do not rely on CD for normal operation.

### Buffering
- **Producer**: Existing 500 Hz frame task pushes `PdlFrameV1` into a queue (current `g_frame_q`). Keep producer non-blocking: if queue full, drop frame and set a **sticky** `FLG_DROPPED_FRAME` in the **next** (or subsequent) frame(s) until a “cleared” policy (e.g. clear after 1 s of no drops). Dropped frames are not written; only the flag indicates loss.
- **Consumer**: SD writer task reads from queue and writes in **large contiguous blocks** (e.g. 8–32 KB) to reduce SD write overhead. Buffer frames in a local buffer (e.g. 256 frames × 56 bytes ≈ 14 KB), then `file.write(buffer, len)`.
- **Flush**: Every **1 s** OR every **256 KB** written, whichever comes first; call `file.flush()`.

### Flag
- Add **FLG_MARK** to **src/format/pdl_flags.h** (e.g. `FLG_MARK = 1u << 11`) for MARK_EVENT (next frame gets this flag set).

### Logger API changes (src/services/sd_logger.h / .cpp)
- `logger_begin()` — Mount SD with 4-bit and setPins; return true only if `cardType() != CARD_NONE`.
- `logger_start_session(const PdlHeaderV1& hdr, bool rtc_valid, uint32_t rtc_epoch)` (or pass a struct with filename info) — Build filename (RTC or RUN####), open .TMP, write header, set “logging” and start accepting frames.
- `logger_stop_session()` — Set “drain” flag; when writer has drained, flush/close and rename .TMP → .BIN.
- `logger_is_logging()` — Already exists.
- `logger_get_last_bin_path()` or similar — For EXPORT_LATEST: path of the last closed .BIN (e.g. same name as .TMP but .BIN).
- Writer task: Loop receive from queue (block with timeout), accumulate into block buffer, write when buffer full or periodic flush; on “drain” request, flush remaining, close, rename.

### Export mode (BIN → CSV on-board)
- Trigger: LONG press in IDLE_READY or STOPPED when a previous .BIN exists (else brief warning blink).
- Flow:
  - Set UI state to EXPORTING; LED PULSE(PURPLE, 1000).
  - Open last .BIN for read (or user-selected file); read header, validate magic; then read frames one-by-one (or small batch), convert each frame to one CSV line, write to a new .CSV file (e.g. same base name, .CSV) or stream to a second file. **Stream line-by-line**: do not load entire BIN into RAM; read 56-byte frames, format line, write line.
- Success: Green double-blink then SOLID(GREEN); return to IDLE_READY or STOPPED.
- Error: Use SD write error pattern (3 red blinks) or a dedicated export-error pattern.
- Optional: SHORT press during EXPORTING = CANCEL_EXPORT (stop conversion, close files, return to IDLE/STOPPED).

### Header usage
- Use existing `PdlHeaderV1` (256 bytes). Set `start_rtc_epoch` when RTC valid; otherwise 0. Frame format `PdlFrameV1` (56 bytes) already has `flags` (include FLG_MARK, FLG_DROPPED_FRAME, etc.). CSV export: header can be a single comment line with column names; each line = one frame (e.g. sample_index, t_us, force_mean_mN, ax, ay, az, …, flags, vbat_mV, soc_centiPct).

---

## D) Main state machine and integration

### UI states (enum)
- `BOOT`, `IDLE_READY`, `LOGGING`, `STOPPED`, `EXPORTING`, `FAULT`.

### Gesture → action mapping (lock in)
- **IDLE_READY**: SHORT → START_LOG; LONG → EXPORT_LATEST (if .BIN exists, else brief warning blink); DOUBLE → SELF_TEST (optional; can no-op).
- **LOGGING**: LONG → STOP_LOG; SHORT → MARK_EVENT (set FLG_MARK on next frame); DOUBLE → NO-OP.
- **STOPPED**: SHORT → START_LOG; LONG → EXPORT_LATEST.
- **EXPORTING**: SHORT → CANCEL_EXPORT (optional); LONG → NO-OP.
- **FAULT**: SHORT → RETRY_INIT (SD remount + sensor re-probe); LONG → NO-OP.

### Where to implement
- **Option A**: A small state machine in **main.cpp** (or **src/services/ui_state.cpp**): `ui_state_t` + `ui_tick()`. In `loop()` or a dedicated task: call `button_tick()`, drain button events, map (state, gesture) → action; execute action (start/stop log, start export, retry init); set LED state/warning/fault via `led_ui` API.
- **Option B**: Central **src/services/ui_state.h/.cpp** that holds current state, handles button events, calls logger and export, and drives LED. Main only calls `ui_init()`, `ui_tick()` (or `ui_run()` in a task).

### Startup
- BOOT → LED PULSE(BLUE). After init (SD mount, sensors if any): if SD OK and no fault → IDLE_READY (SOLID(GREEN)); else FAULT.

### MARK_EVENT
- When SHORT in LOGGING: set a flag “mark next frame”. Frame builder (adc_frames.cpp) checks this flag and ORs `fr.flags |= FLG_MARK` for the next produced frame, then clears the “mark next” flag.

### RETRY_INIT
- Call `logger_begin()` again (remount SD); optionally re-probe sensors (e.g. IMU, RTC). If success → IDLE_READY; else stay FAULT.

---

## E) File change summary (for implementation)

| File | Action |
|------|--------|
| **src/services/button.h** | New — API for button and gestures. |
| **src/services/button.cpp** | New — Debounce + gesture FSM, event queue. |
| **src/services/led_ui.h** | New — Pattern/color/state/warning/fault API. |
| **src/services/led_ui.cpp** | New — WS2812 output + pattern engine. |
| **src/format/pdl_flags.h** | Add FLG_MARK. |
| **src/services/sd_logger.h** | Extend API: start_session with filename policy, stop with rename, get last BIN, export. |
| **src/services/sd_logger.cpp** | setPins + 4-bit begin; .TMP naming (RTC or RUN####); drain + rename; block writes; flush 1s/256KB; export BIN→CSV. |
| **src/services/frame_pipe** (or adc_frames) | On queue full: drop frame, set sticky FLG_DROPPED_FRAME; support “mark next” for FLG_MARK. |
| **src/services/adc_frames.cpp** | When “mark next” set, OR FLG_MARK into next frame and clear flag. |
| **src/main.cpp** or **src/services/ui_state.cpp** | State machine: BOOT/IDLE_READY/LOGGING/STOPPED/EXPORTING/FAULT; poll button, map gestures to actions, drive LED. |
| **platformio.ini** | Add WS2812/NeoPixel lib if not in framework (or use Arduino ESP32 built-in). |

---

## F) Constraints recap

- **No WiFi/WebUI.**
- **Primary log remains binary**; CSV only for export.
- **Do not block 64 ksps acquisition** on SD writes (producer never blocks; consumer drains queue in background).
- **Saturated LED colors only**; fixed low brightness default (e.g. 96/255).
- **Single button, single WS2812**; robust for gloves and outdoor use.

---

## G) Implementation order

1. **pdl_flags.h** — Add FLG_MARK.
2. **button.h / button.cpp** — Debounce + gestures + event queue; no UI yet.
3. **led_ui.h / led_ui.cpp** — Primitives (SOLID, BLINK, N_BLINK, PULSE, DOUBLE_BLINK), colors, priority, no delay().
4. **sd_logger** — setPins + 4-bit begin; filename (RTC / RUN####); drain + rename .TMP→.BIN; block writes; flush policy; export BIN→CSV.
5. **frame_pipe / adc_frames** — Overflow → FLG_DROPPED_FRAME; “mark next” → FLG_MARK.
6. **Main state machine** — States, gesture→action, call logger start/stop/export and LED set state/warning/fault.
7. **Integration** — Wire button and LED GPIO in main or config; ensure BOOT → IDLE_READY → LOGGING/STOP/EXPORT/FAULT flows and LED matches spec.
8. **Build and test** — Clean build; verify gesture mapping and file lifecycle and export.
