# Loadcell Datalogger AI Coding Instructions

## Project Overview
**Loadcell_Datalogger** is an embedded systems firmware for an ESP32-S3 devkit that acquires load cell, IMU, and RTC data and logs it to an SD card. The system uses a state machine pattern with interrupt-driven peripherals.

**Key Hardware:**
- **MCU**: ESP32-S3 (PSRAM enabled, native USB)
- **Sensors**: LSM6DSV16X IMU (I2C, 960 Hz ODR @ ±8g accel), RX8900 RTC (I2C), MAX11270 external ADC (SPI)
- **Storage**: SD_MMC 4-bit interface, NeoPixel status LED (GPIO 21)
- **I/O Pins**: GPIO 41–42 (I2C), GPIO 2 (logstart button), GPIO 4–9 (SD_MMC CLK/CMD/D0–D3), GPIO 12–18 (SPI for ADC)

## Architecture & Data Flow

### System State Machine (main.cpp)
The firmware operates in three states:
1. **STATE_INIT** (Red LED): Peripherals initializing
2. **STATE_READY** (Green LED): Waiting for logstart button press
3. **STATE_LOGGING** (Green LED): Actively acquiring and storing data

Button press (GPIO 2, external pulldown) triggers READY→LOGGING transition. The RTC interrupt (GPIO 34, 1 Hz update) drives periodic operations via the `g_rtcUpdatePending` flag.

### Component Initialization Order (initPeripherals)
1. **SD Card** (SD_MMC 4-bit): Must configure pins explicitly via `SD_MMC.setPins()` before `SD_MMC.begin()`. Check CD pin (GPIO 10, active-low) first.
2. **IMU** (imu.cpp): Initialize on I2C after Wire is started. Default I2C address is 0x6B; verify SDO/SA0 pin if init fails.
3. **RTC** (RX8900): Attach FALLING edge interrupt on GPIO 34. Configure for 1 Hz update interrupt (UIE flag).
4. **ADC** (MAX11270): Currently a TODO stub.

### Interrupt Handling
- **RTC INT (GPIO 34)**: Fires every 1 second, sets `g_rtcUpdatePending` flag. ISR must be IRAM_ATTR and keep logic minimal; real processing happens in `handleRtcUpdate()` main context.
- **IMU INT1/INT2 (GPIO 39/40)**: Wired but currently unused (polling only). To enable, configure data-ready interrupts via `imuDevice.setAccelStatusInterrupt(true)` and `imuDevice.int1RouteAccelDataReady(true)`.

## Key Code Patterns & Conventions

### Logging Format
All debug output uses a consistent prefix: `[COMPONENT]` or `[STATE]` for state transitions.
- Examples: `[INIT][SD]`, `[IMU]`, `[RTC]`, `[LOG]`, `[ERROR]`
- Use `Serial.printf()` with `%d`, `%llu` for numeric output.

### Pin Configuration (pins.h)
All GPIO assignments are centralized in `pins.h`. When adding hardware:
1. Define the pin as a `static const gpio_num_t` with a descriptive comment.
2. Pin names follow pattern: `PIN_<SUBSYSTEM>_<SIGNAL>`.
3. Do NOT hardcode pin numbers in source files; always reference via `pins.h`.

### IMU Configuration (imu.cpp)
- **FS ± 8 g** (accel), **± 2000 dps** (gyro) chosen for load cell shock (peak ~3 g over ~10 ms).
- **ODR 960 Hz** ensures ~9–10 samples across a 10 ms event; Nyquist ≈ 480 Hz.
- **LP2 filter** (accel, STRONG bandwidth) tames high-frequency noise; LP1 filter (gyro, ULTRA_LIGHT) minimizes latency.
- **Block Data Update** enabled: accel + gyro registers update together (prevents partial reads).
- `imuRead()` polls `checkStatus()` before reading to skip stale samples.

### SD Card Initialization (main.cpp)
- Card detect (CD) pin logic: `digitalRead(PIN_SD_CD) == HIGH` means no card.
- After `SD_MMC.setPins()`, MUST call `SD_MMC.begin("/sdcard", false, false)` (4-bit mode, no auto-format).
- `/log` directory is created if missing; non-critical failure (logged as WARNING).

### RTC Usage (main.cpp, initRtc)
- 1 Hz interrupt via `UpdateInterruptTimingChange(false)`.
- ISR sets flag; main loop calls `handleRtcUpdate()` to clear interrupt status.
- `rx8900.JudgeInterruptSignalType(&flagChange)` reads which flags fired; always clear explicitly via `ClearOccurrenceNotification()`.

### Global Variables & Extern
- `imuDevice` (imu.cpp): Global SparkFun_LSM6DSV16X instance, initialized in `imuInit()`.
- `statusPixel` (main.cpp): Adafruit_NeoPixel singleton for LED control.
- `rx8900` (main.cpp): Global RTC instance.
- `g_rtcUpdatePending` (main.cpp): Volatile flag set by RTC ISR; cleared in main context.

## Build & Upload

### Commands
- **Build**: `pio run -e esp32-s3-devkitc1-n4r2`
- **Upload**: `pio run -e esp32-s3-devkitc1-n4r2 -t upload` (uses native USB)
- **Monitor**: `pio device monitor -b 115200`

### Environment Config (platformio.ini)
- Custom platform: `https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip`
- **Critical Build Flags**:
  - `ARDUINO_USB_MODE=1`: Native USB enabled
  - `ARDUINO_USB_CDC_ON_BOOT=1`: Serial console available on boot
  - `CORE_DEBUG_LEVEL=5`: Debug logging enabled (verbose)
- Memory: PSRAM enabled, QIO mode, 4 MB flash.
- Monitor baud: 115200.

## Development Tips

1. **First-time SD Card Issues**: Verify all six SD_MMC pins are connected correctly (CLK, CMD, D0–D3). Test card detect pin independently before power-cycling device.

2. **IMU I2C Address**: If `imuDevice.begin()` fails, check SDO/SA0 strapping. Default is 0x6B; low-strap → 0x6A. Modify library address or use alternate `begin(0x6A)` if available.

3. **RTC Interrupt Timing**: The "1 Hz update" fires asynchronously; don't assume it aligns with `millis()`. Use RTC time registers (read in `handleRtcUpdate()`) for accurate epoch tracking if needed.

4. **ISR Performance**: Keep interrupt handlers (IRAM_ATTR) short. Complex logic (I2C reads, FS ops) must run in main context after ISR sets a flag.

5. **Future Logging Logic** (loggingTick): Combine RTC 1 Hz tick with IMU 960 Hz data. Consider buffering IMU samples between RTC ticks and writing a single SD block per second to minimize SD latency.

## External Dependencies
- **Adafruit_NeoPixel**: Status LED control
- **RX8900**: RTC library (custom or vendor)
- **SparkFun_LSM6DSV16X**: IMU library (v1.0.2+)
- **ESP32 Arduino Core** (via pioarduino platform): SD_MMC, Wire, native USB

## File Structure
```
src/
  main.cpp          ← Setup, state machine, SD/RTC/ADC init, button handling, LED control
  imu.cpp           ← IMU initialization and polling (960 Hz accel/gyro)
include/
  imu.h             ← IMU function prototypes and extern imuDevice
  pins.h            ← All GPIO definitions (centralized)
platformio.ini      ← Build config, dependencies, platform URL
```
