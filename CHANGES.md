# ESP32-S3 Build Fix and Driver Integration — File Summary

## Build fixes
- **src/services/sensors_task.cpp** — Fixed object declarations (no parentheses for `fuel`, `rtc`, `imu`). Aligned driver API: `begin(Wire)`, `imu.configure(Odr::HZ_960, Odr::HZ_960)`, `readRaw(SampleRaw)`, `readVoltage_mV`/`readSOC_centiPercent`, `readDateTime(DateTime)`.
- **src/main.cpp** — Included `sensors_task.h` and call `start_sensors_task()` in `setup()`.

## Snapshot integration
- **src/services/adc_frames.cpp** — Included `aux_state.h`. Frame loop uses `aux_get_snapshot()` for `fr.ax`…`fr.soc_centiPct` (IMU, batt) instead of a static uninitialized buffer.

## SD_MMC 4-bit
- **src/services/sd_logger.cpp** — `SD_MMC.setPins(4, 5, 6, 7, 8, 9)` before `begin`. `SD_MMC.begin("/sdcard", false)` (4-bit mode). `cardType() != CARD_NONE` check after `begin`.

## Driver additions
- **src/drivers/lsm6dsv.h, lsm6dsv.cpp** — `setIntPinConfig(activeLow, openDrain)` (CTRL3), `routeDrdyToInt1(accel, gyro)` (INT1_CTRL 0x0D).
- **src/drivers/rx8900ce.h, rx8900.cpp** — `enableSecondUpdateInterrupt(enable)` (USEL=0, UIE=1), `setFoutFrequency(hz1)`. FLAG_UF etc. made public for `clearFlags()`.

## IMU/RTC interrupts and sensors task
- **src/services/sensors_task.cpp** — IMU INT1 on GPIO39 (rising): ISR increments volatile counter (no I2C). RTC /INT on GPIO34 (falling): ISR sets volatile pending flag (no I2C). After init: `imu.setIntPinConfig(false, false)`, `imu.routeDrdyToInt1(true, true)`, `rtc.enableSecondUpdateInterrupt(true)`; `attachInterrupt` for both pins. Task: on IMU count change read one `readRaw()` and update snapshot; when RTC pending read RTC, update snapshot, clear FLAG_UF and pending. Fuel gauge polled every 1 s unchanged.

## Unchanged (per plan)
- **src/drivers/max11270.cpp** — GPIO init, `startConversions` rate storage, `readSampleBlocking` yield already correct; no edits.
- No new external libraries; Arduino-ESP32 / ESP-IDF only.
