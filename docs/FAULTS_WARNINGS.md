# Fault and warning propagation

Central status is in **system_status** (`src/services/system_status.h` / `.cpp`). The UI reads it each tick and drives the LED (FAULT > WARNING > STATE). Faults persist until cleared by retry (short press in FAULT state).

## Fault codes (LED: red N-blink)

| Code | LED pattern | Set by | Cleared by |
|------|-------------|--------|------------|
| **SD_MOUNT_FAIL** | 2-blink | **sd_logger**: `logger_begin()` when `SD_MMC.begin` fails, `cardType() == CARD_NONE`, or (if `SD_CD_ENABLED`) card-detect pin says no card | Retry: `logger_begin()` success |
| **SD_WRITE_FAIL** | 3-blink | **sd_logger**: file open fail in `logger_start_session()`, header write fail, write fail in logger task, rename fail in `do_rename_tmp_to_bin()`, export open/read fail in `logger_export_latest_to_csv()`. Also when SD card-detect indicates card removed during logging | Retry: `logger_begin()` success |
| **ADC_FAULT** | 4-blink | **adc_frames**: `readSampleBlocking()` returns error (timeout/SPI) | Not cleared by retry (ADC re-init would require task restart) |
| **IMU_FAULT** | 5-blink | **sensors_task**: IMU `begin`/`configure` fails at start | Retry: sensor re-probe success |

## Warning codes (LED: state-dependent pattern)

| Code | Set by | Cleared by |
|------|--------|------------|
| **RTC_INVALID** | **adc_frames**: per-frame when `!snap.rtc_valid` | **adc_frames**: when `snap.rtc_valid` is true |
| **RTC_FAULT** | **sensors_task**: when RTC `readDateTime` fails in the RTC interrupt handler | **sensors_task**: when RTC read succeeds; or retry: RTC re-probe success |
| **LOW_BATT** | **adc_frames**: per-frame when SOC or voltage below config thresholds | **adc_frames**: when above thresholds |
| **UNDERLOAD** | **adc_frames**: per-frame when force &lt; underload threshold | **adc_frames**: when above threshold |
| **OVERLOAD** | **adc_frames**: per-frame when force &gt; overload threshold | **adc_frames**: when below threshold |
| **COMPRESSION** | **adc_frames**: per-frame when force &lt; -compression threshold | **adc_frames**: when above threshold |
| **IMU_WARN** | (Reserved; IMU currently treated as fault when missing) | — |

## RETRY (short press in FAULT)

- Calls `logger_begin()` (SD remount). On success, clears **SD_MOUNT_FAIL** and **SD_WRITE_FAIL**.
- Calls `sensors_request_retry_probe()`. The sensors task re-runs `begin` for IMU and RTC; on success it clears **IMU_FAULT** and **RTC_FAULT**.
- If no fault remains after retry, UI goes to IDLE_READY. If faults remain (e.g. ADC), state stays FAULT. When the sensors task later clears IMU/RTC faults, the next tick will see no fault and transition to IDLE_READY.

## Optional SD card-detect (GPIO10)

- **config.h**: `SD_CD_ENABLED` (default `false`), `SD_CD_ACTIVE_LOW` (default `true`).
- **pins.h**: `PIN_SD_CD = 10`.
- When enabled: at mount, if CD says “no card”, fail with SD_MOUNT_FAIL. During logging, if CD says “no card”, set SD_WRITE_FAIL and request drain (stop session, close file). Mount/cardType remain authoritative when CD is disabled or indicates card present.
