# H562 Parachute Datalogger — Firmware Plan (Rev 5)

## Context

Bare-metal firmware for a parachute deployment force measurement system. Goal: no lost ADC samples, no lost logged records, no lost IMU samples.

**Current system state (post Phase 7 + SYSCLK upgrade):**
- SYSCLK = 250 MHz (HSI→PLL: PLLM=4, PLLN=31, FRACN=2048, PLLP=2), VOS0, FLASH_LATENCY_5, ICACHE=ON
- PLL1Q = 50 MHz (PLLQ=10) → SPI1 clock source (unchanged)
- PLL2P = 20 MHz → SPI2 clock source
- SPI1 prescaler = /4 → **12.5 MHz** ✓ (ADS131M02 max 25 MHz, 50% margin)
- SPI2 prescaler = /2 → **10.0 MHz** ✓ (LSM6DSV max 10 MHz, DOE-validated)
- HSI/LSI only — no external crystals (shock-proof) ✓
- USBX CDC ACM in **standalone mode** (`UX_STANDALONE` defined) — no ThreadX scheduler ✓
- ThreadX compiled/linked as utility library only — scheduler never started ✓
- ISR handlers present but empty ✓ (no application code yet, clean start)
- FatFS operational — DMA-based sd_diskio, 25 MHz SDMMC, ~325 KB/s throughput ✓
- USBX CDC operational — "ValueXT Loadcell Datalogger" in Device Manager ✓

---

## Root Cause of Previous Sample Loss

**SPI clock violations — not a DMA or RTOS issue:**

| Device | Spec Max SPI | Previous Config | Fixed Config | Status |
|--------|-------------|----------------|-------------|--------|
| ADS131M02 | 25 MHz | 25 MHz | **12.5 MHz** | ✓ 50% margin |
| LSM6DSV | 10 MHz | 25 MHz | **10.0 MHz** | ✓ DOE-validated |
| LTC6903 | 20 MHz | 25 MHz | **12.5 MHz** | ✓ 37.5% margin |

**Fix applied in IOC:** SPI1 → /4 = 12.5 MHz. SPI2 → /2 = 10.0 MHz (via PLL2P = 20 MHz, DOE-validated).

---

## Architecture: Bare-Metal with DMA (no RTOS scheduler)

- ADC hot path: DRDY EXTI → SPI1 DMA → DMA-complete ISR → two-stage decimation (8-sample → 8 kHz ADC record, 128-sample → 500 Hz Force record + CSV line)
- IMU: blocking SPI2 at decimation boundary (500×/s, ~14 µs at 10.0 MHz for 14 bytes)
- SD writes: FatFS f_write() in main loop, absorbed by 256 KB ring buffer (binary) + 1 KB CSV line buffer
- USB CDC: USBX standalone (polling) — `ux_system_tasks_run()` in main loop, debug printf only
- Battery: ADC1 polling every 60 s in main loop — no DMA, no contention
- NeoPixel: TIM2 CH1 PWM + DMA, one-shot on state change only — uses separate GPDMA channel
- Dual-file SD logging: binary (.bin) at 8 kHz ADC + 500 Hz Force/IMU + 1 Hz metadata; CSV (.csv) at 500 Hz framed force data
- State machine: IDLE → LOGGING → IDLE

### USB Decision: USBX Standalone (Resolved)

**CubeMX already generated USBX with `UX_STANDALONE` mode.** This means:
- ThreadX is compiled/linked but **its scheduler never starts** (no `tx_kernel_enter()`)
- ThreadX only provides utility functions (`tx_interrupt_control`) used internally by USBX
- USBX runs as a **polling state machine** — `ux_system_tasks_run()` called in main `while(1)` loop
- Zero RTOS overhead: no thread switching, no tick handler, no priority inversion, no stack overhead
- CDC ACM class already registered with callbacks

**Why this won't affect sampling:**
- ADC DRDY EXTI runs at NVIC priority 0 — preempts everything including USB interrupts
- `ux_system_tasks_run()` is non-blocking — processes one USB event per call, returns immediately
- USB interrupt handler (`USB_DRD_FS_IRQHandler`) runs at lower NVIC priority than EXTI2/SPI1 DMA
- Battery ADC1 polling is once per 60 s — negligible
- NeoPixel DMA is one-shot, ~30 µs per LED update, only on state transitions

**Required to activate USB CDC:**
1. Fix `_ux_utility_time_get()` in `app_usbx.c` USER CODE section: return `HAL_GetTick()`
2. Register USB DCD after USBX init: `ux_dcd_stm32_initialize((ULONG)USB_DRD_FS, (ULONG)&hpcd_USB_DRD_FS)`
3. Call `ux_system_tasks_run()` in main `while(1)` loop
4. Implement CDC transmit wrapper using `ux_device_class_cdc_acm_write_run()` (non-blocking)

---

## IOC Changes — All Applied ✓

All IOC changes from previous plan have been applied and verified:

| # | Setting | Value | Verified |
|---|---------|-------|---------|
| 1 | PLL Source | HSI (64 MHz) | ✓ `SystemClock_Config` |
| 2 | PLLM | 4 | ✓ |
| 3 | PLLN | 31, FRACN=2048 | ✓ (upgraded from 9/3072) |
| 4 | PLLP | 2 → SYSCLK 250 MHz | ✓ (upgraded from 75 MHz, VOS0) |
| 5 | PLLQ | 10 → PLL1Q 50 MHz (SPI1 source) | ✓ (PLL1Q unchanged) |
| 6 | PLL2P | 20 MHz (SPI2 source) | ✓ |
| 7 | RTC Clock | LSI | ✓ |
| 8 | HSE/LSE | Disabled | ✓ no crystal refs |
| 9 | SPI1 Prescaler | /4 → 12.5 MHz | ✓ `SPI_BAUDRATEPRESCALER_4` |
| 10 | SPI2 Prescaler | /2 → 10.0 MHz | ✓ `SPI_BAUDRATEPRESCALER_2` (DOE-validated) |
| 11 | SPI2 DMA | Removed — GPDMA CH0/CH1 only (SPI1) | ✓ |
| 12 | USBX CDC ACM | Standalone mode (`UX_STANDALONE`) | ✓ |

**One more CubeMX regen planned (Phase 9a+9b combined):** Add GPDMA1 CH2 for TIM2_UP (NeoPixel), disable I2C1, add CHG_PG/CHG_STAT1/CHG_STAT2 GPIOs. Linker script is safe to modify after this regen.

---

## CubeMX-Safe Code Rule

**Only write application code inside `/* USER CODE BEGIN */` / `/* USER CODE END */` sections** in CubeMX-generated files. These sections survive regeneration.

All new `.c`/`.h` files created outside CubeMX-generated filenames are always safe. When a change IS needed outside a USER CODE section, this plan will explicitly state:
- Which file and which line
- What to change
- What CubeMX will overwrite on next regen
- How to restore it

---

## USB DFU (Program Without STLink)

STM32H562 has a ROM bootloader (factory-programmed at system memory) that supports USB DFU. No code change to the bootloader itself — it's built into the chip.

**How to enter DFU mode from application:**

If `userButton (PC13)` is held at power-on, jump to ROM bootloader:

```c
// In main.c, USER CODE BEGIN 2 (runs before any peripheral init)
/* USER CODE BEGIN 2 */
if (HAL_GPIO_ReadPin(userButton_GPIO_Port, userButton_Pin) == GPIO_PIN_RESET) {
    // Button held — jump to USB DFU bootloader
    __HAL_RCC_USB_CLK_ENABLE();   // Ensure USB clock is on
    HAL_Delay(100);
    // Disable SysTick and all interrupts
    SysTick->CTRL = 0;
    __disable_irq();
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    // Set MSP and jump to STM32H562 system memory bootloader
    const uint32_t BOOTLOADER_ADDR = 0x0BF97000UL;  // STM32H5 system memory
    __set_MSP(*(uint32_t *)BOOTLOADER_ADDR);
    ((void (*)(void))(*(uint32_t *)(BOOTLOADER_ADDR + 4)))();
}
/* USER CODE END 2 */
```

**On Windows:** Install `STM32CubeProgrammer`. Connect USB. Hold userButton, press reset. CubeProgrammer detects the DFU device → select `.bin` or `.elf` → program.

> Verify the bootloader address `0x0BF97000` against STM32H562 Reference Manual RM0481 Table "Memory map." If it is different, share a screenshot of the RM table.

**This code is in a USER CODE section — CubeMX safe.**

---

## IMU Data: Raw vs Orientation

**Decision: Log raw accel + gyro (int16_t) now. Add quaternion in a later phase.**

Rationale:
- Raw values are the ground truth — never discard them
- The LSM6DSV has a built-in hardware sensor fusion engine (SFLP — Sensor Fusion Low Power) that outputs quaternions without CPU cost. This is better than running MotionFX on the CPU.
- For Phase 1, raw data suffices. Add SFLP quaternion output in a dedicated later phase once basic logging is verified.
- MotionFX libraries remain available if SFLP proves insufficient.

When quaternion is added (later): add `float quat[4]` (16 bytes) to `bin_force_record_t`. Total Force+IMU record size would go from 32 to 48 bytes (16 KB/s -> 24 KB/s — still well within SD ceiling).

---

## Dual-File Logging Format

Two files are written simultaneously during each logging session:

1. **CSV** (`LOG_YYMMDD_HHMMSS.csv`) -- framed 500 SPS force data for immediate consumption
2. **Binary** (`LOG_YYMMDD_HHMMSS.bin`) -- high-fidelity 8 kSPS ADC + 500 SPS Force/IMU + 1 Hz metadata for post-processing

### CSV Format (500 SPS)

Header (comment lines, skipped by parser):
```
# CLKIN=8190457 Hz, DAC=983, SYSCLK=250000000 Hz
# sensitivity=2.000 uV/N, tare=0.000 N, gains=1/1
```

Data lines (500/s), framed with `$` and `#` delimiters:
```
$,0,+0.000,#
$,2,+0.001,#
$,4,+0.003,#
```

Each line: `$,<time_ms>,<load_N>,#\r\n` (~22 bytes/line, 500 lines/s = ~11 KB/s)

### Binary File Header (64 bytes)

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;             // 'LDCL' = 0x4C44434C
    uint16_t version;           // Format version (1)
    uint16_t header_size;       // sizeof(bin_file_header_t)
    uint32_t clkin_hz;          // Measured CLKIN at session start
    uint32_t sysclk_hz;        // SYSCLK frequency
    uint16_t ltc_dac;          // Trimmed DAC value
    uint16_t adc_osr;          // ADC oversampling ratio (128)
    uint16_t adc_record_rate;  // 8000
    uint16_t force_record_rate;// 500
    float    sensitivity;      // uV/N from config.txt
    float    tare_offset;      // N from config.txt
    uint8_t  adc_gain_ch1;     // ADS131M02 gain setting
    uint8_t  adc_gain_ch2;
    uint8_t  imu_odr;          // LSM6DSV ODR setting (500 Hz)
    uint8_t  imu_fs_accel;     // Full-scale accel (16g)
    uint32_t rtc_epoch;        // RTC time at start (seconds since 2000-01-01)
    uint8_t  fw_version[8];    // e.g., "v0.5.0\0\0"
    uint8_t  reserved[12];     // Pad to 64 bytes
    uint16_t crc16;            // CRC of bytes 0..61
} bin_file_header_t;           // 64 bytes
```

### Record Type 0x01: ADC (16 bytes, 8000/s = 128 KB/s)

```c
typedef struct __attribute__((packed)) {
    uint8_t  type;             // 0x01
    uint8_t  flags;            // bit 0: ADS CRC OK, bit 1: overflow
    uint16_t seq_lo;           // Low 16 bits of 8 kHz counter
    int32_t  sum_ch1;          // Sum of 8 raw 24-bit ADC samples
    int32_t  sum_ch2;          // Sum of 8 raw 24-bit ADC samples
    uint16_t crc16;            // CRC of bytes 0..13
} bin_adc_record_t;            // 16 bytes
```

### Record Type 0x02: Force+IMU (32 bytes, 500/s = 16 KB/s)

```c
typedef struct __attribute__((packed)) {
    uint8_t  type;             // 0x02
    uint8_t  validity;         // Validity flags (see below)
    uint16_t seq_lo;           // Low 16 bits of 500 Hz counter
    uint32_t timestamp_ms;     // ms since logging start
    float    force_N;          // Computed ratiometric force
    int16_t  accel_x;          // LSM6DSV raw
    int16_t  accel_y;
    int16_t  accel_z;
    int16_t  gyro_x;
    int16_t  gyro_y;
    int16_t  gyro_z;
    int32_t  sum_ch1_128;      // Full 128-sample ADC sum (for recalculation)
    uint16_t crc16;            // CRC of bytes 0..29
} bin_force_record_t;          // 32 bytes
```

### Record Type 0x03: Metadata (32 bytes, 1/s = 32 B/s)

```c
typedef struct __attribute__((packed)) {
    uint8_t  type;             // 0x03
    uint8_t  reserved;
    uint16_t second_num;       // Seconds since logging start
    uint32_t clkin_hz;         // Measured CLKIN this second
    int16_t  mcu_temp_x10;    // MCU die temp x10 (235 = 23.5 C)
    uint16_t battery_mv;       // Battery voltage in mV
    uint32_t drdy_total;       // Cumulative DRDY count
    uint32_t miss_total;       // Cumulative missed DRDYs
    uint32_t overflow_total;   // Cumulative ring overflows
    uint16_t ads_status;       // Last ADS131M02 STATUS word
    uint16_t padding;
    uint16_t crc16;
} bin_meta_record_t;           // 32 bytes
```

### Validity Flags (used in `bin_force_record_t.validity`)

```c
#define VALIDITY_ADC_OK       0x01  // ADS STATUS word had no error flags
#define VALIDITY_IMU_OK       0x02  // IMU read succeeded
#define VALIDITY_BATT_FRESH   0x04  // Battery reading < 60 s old
#define VALIDITY_NO_OVERFLOW  0x08  // Ring buffer had no overflow since last record
#define VALIDITY_ADS_CRC_OK   0x10  // ADS frame CRC verified (if enabled)
#define VALIDITY_CAL_DEFAULT  0x20  // Using hardcoded defaults
```

### SD Throughput Budget

| Stream | Rate | Record Size | Throughput |
|--------|------|-------------|------------|
| Binary ADC | 8,000/s | 16 B | 128.0 KB/s |
| Binary Force+IMU | 500/s | 32 B | 16.0 KB/s |
| Binary Metadata | 1/s | 32 B | ~0 KB/s |
| CSV lines | 500/s | ~22 B | ~11 KB/s |
| **Total** | | | **~155 KB/s** |

**62% of the proven 250 KB/s SD ceiling.** Comfortable margin for FAT write stalls and USB CDC overhead.

### Ring Buffer Sizing

- Binary data rate: ~144 KB/s
- Buffer size: **256 KB** (power-of-2 for efficient wrap-around masking)
- Hold time: ~1.8 seconds at full rate
- RAM budget: 640 KB total, ~100 KB estimated current use, 256 KB ring buffer, ~280 KB remaining

### File Sizes (1-hour session)

| File | Rate | Size/hour |
|------|------|-----------|
| Binary (.bin) | 144 KB/s | ~518 MB |
| CSV (.csv) | 11 KB/s | ~40 MB |
| **Total** | 155 KB/s | **~558 MB** |

64 GB card fits ~114 hours of logging.

---

## BQ24012 Battery Charger Status Monitoring

The BQ24012DRCR provides three output signals. Reading them gives pre-flight charge confirmation, charging state for the VT220 UI, and post-analysis context.

**Pin Assignments:**

| Signal | BQ24012 Pin | MCU Pin | Label in IOC | Rationale |
|--------|-------------|---------|-------------|-----------|
| PG (Power Good) | PG (active low, open-drain) | **PC14** | `CHG_PG` | Freed from LSE. Port C clock already enabled (many Port C pins in use). |
| STAT1 | STAT1 (push-pull) | **PC15** | `CHG_STAT1` | Freed from LSE. Adjacent to PC14 — easy routing. |
| STAT2 | STAT2 (push-pull) | **PB6** | `CHG_STAT2` | Currently assigned to I2C1_SCL but I2C1 is unusable — PB7 (SDA) is not bonded on LQFP64. Reclaim in IOC: disable I2C1, set PB6 as GPIO_Input. |

**Why these pins:**
- PC14 + PC15: freed when LSE was disabled. No alternate function conflict. Port C GPIO clock is already running.
- PB6: currently wasted on a dead I2C1 peripheral (SDA pin not bonded on this package). Repurposing it has zero functional cost. Port B GPIO clock already running.
- All three pins are on already-active GPIO ports → no additional clock enable overhead.

**IOC change required:** Disable I2C1 peripheral. Set PB6 mode to `GPIO_Input`, label `CHG_STAT2`. Set PC14 to `GPIO_Input` with `GPIO_PULLUP` (PG is open-drain active-low), label `CHG_PG`. Set PC15 to `GPIO_Input`, label `CHG_STAT1`.

**Decode table:**

| PG | STAT1 | STAT2 | Meaning | UI String |
|----|-------|-------|---------|-----------|
| LOW | LOW | HIGH | Charging (from USB/ext) | `CHG: CHARGING` |
| LOW | HIGH | LOW | Charge complete | `CHG: FULL` |
| LOW | HIGH | HIGH | Standby / fault | `CHG: STANDBY` |
| HIGH | x | x | No input power (battery only) | `CHG: BATTERY` |

**Implementation (in `battery_monitor.c`):**
```c
typedef enum {
    CHG_BATTERY,    /* No input power — running on battery */
    CHG_CHARGING,   /* Input power present, charging */
    CHG_FULL,       /* Input power present, charge complete */
    CHG_STANDBY,    /* Input power present, standby or fault */
} charge_state_t;

charge_state_t battery_get_charge_state(void)
{
    /* PG is active-low open-drain: LOW = power good */
    if (HAL_GPIO_ReadPin(CHG_PG_GPIO_Port, CHG_PG_Pin) == GPIO_PIN_SET)
        return CHG_BATTERY;  /* No input power */

    uint8_t s1 = HAL_GPIO_ReadPin(CHG_STAT1_GPIO_Port, CHG_STAT1_Pin) == GPIO_PIN_RESET;
    uint8_t s2 = HAL_GPIO_ReadPin(CHG_STAT2_GPIO_Port, CHG_STAT2_Pin) == GPIO_PIN_RESET;

    if (s1 && !s2)  return CHG_CHARGING;
    if (!s1 && s2)   return CHG_FULL;
    return CHG_STANDBY;
}
```

**Polling frequency:** Same as battery voltage — every 60 s in main loop. No interrupt needed.

**VT220 UI:** Add to Row 14 of the status panel:
```
║    Vbat: 3.72 V    SD: READY   Cal: SD-FILE   CHG: FULL   ║
```

**Log record impact:** None. Charge state is informational — displayed on VT220 UI and included in the copy-pastable status report only. Not logged at 500 sps.

---

## Phase-by-Phase Bringup Plan

Each phase has explicit **success criteria** that must be met before proceeding.

### Phase Gate Rule (NON-NEGOTIABLE)

**No work on Phase N+1 may begin until ALL success criteria for Phase N are checked off and verified.** This means:
- Every `[ ]` checkbox in the phase's success criteria must be converted to `[x]` with evidence (serial output, file validation, measurement)
- If a criterion cannot be met, the phase is not complete — fix the issue before moving on
- Partial completion is not acceptable; all criteria are mandatory
- The developer must explicitly confirm phase completion before starting the next phase

### Cross-Cutting Rules (Apply to ALL Phases)

**1. Serial debug from Phase 1 onward:**
Every phase that adds a new peripheral or driver MUST print its init status and runtime data to the serial terminal (USB CDC + USART1). This is not optional — it's the primary debugging tool. Examples:
- Phase 4 (FatFS): `printf("SD: mounted, free=%lu MB\r\n", ...)`
- Phase 5 (LTC6903): `printf("CLKIN: %lu Hz\r\n", measured_freq)`
- Phase 6 (ADS131M02): `printf("ADS: ID=0x%04X STATUS=0x%04X\r\n", id, status)`
- Phase 7 (ADC continuous): periodic `printf("DRDY: %lu Hz, misses: %lu\r\n", ...)`
- Phase 8 (IMU): `printf("IMU: WHO_AM_I=0x%02X ax=%.2f ay=%.2f az=%.2f\r\n", ...)`
- Phase 9 (Battery): `printf("BATT: %.2fV (%u%%) CHG:%s USB:%s\r\n", ...)`

These `printf()` calls feed into VT220 UI fields AND the scrolling debug log below the panel. When the VT220 panel is drawn (Phase 2+), field updates replace the `printf` calls with cursor-positioned writes. The scrolling log (`ui_log()`) is used for one-shot events (init, errors, state changes).

**2. USB always connected during development:**
The developer will have USB connected from Phase 1 through Phase 13. All phases must work with USB CDC active. The `allow_log_on_usb = 1` setting in config.txt is the **default** during development. The USB logging gate (Phase 9) only blocks logging when `allow_log_on_usb = 0` (production).

**3. Every init function must return a status and print it:**
```c
/* Pattern for all driver init functions */
int xxx_init(void) {
    /* ... hardware init ... */
    if (error) {
        printf("XXX: init FAILED (reason)\r\n");
        return -1;
    }
    printf("XXX: init OK (key parameters)\r\n");
    return 0;
}
```
Init failures are non-fatal for non-critical peripherals (battery, neopixel). Fatal for ADC, SD card during logging.

---

### Phase 0 — IOC Fixes and Clock Verification ✅ COMPLETE
**Goal:** Correct SPI clocks, crystal-free clocking, clean project state.

All IOC changes applied and verified. See "IOC Changes" table above.

**Remaining pre-code task:** Modify linker script (`STM32H562RGTX_FLASH.ld`):
- `_Min_Heap_Size = 0x4000` (16 KB, was 512 bytes — needed for printf %f with newlib-nano)
- `_Min_Stack_Size = 0x2000` (8 KB, was 4 KB — FatFS + printf + USB call chain)

---

### Phase 1 — USB CDC + Debug Printf
**Goal:** Windows sees a COM port; `printf()` output appears in terminal with correct formatting.

**Actions:**
1. Fix `_ux_utility_time_get()` in `USBX/App/app_usbx.c` (USER CODE section):
   ```c
   /* USER CODE BEGIN _ux_utility_time_get */
   time_tick = HAL_GetTick();
   /* USER CODE END _ux_utility_time_get */
   ```
2. Register USB DCD controller in `main.c` USER CODE BEGIN 2:
   ```c
   ux_dcd_stm32_initialize((ULONG)USB_DRD_FS, (ULONG)&hpcd_USB_DRD_FS);
   HAL_PCD_Start(&hpcd_USB_DRD_FS);
   ```
3. Add `ux_system_tasks_run()` in main `while(1)` loop (USER CODE BEGIN 3)
4. Implement `debug_uart.c/.h`:
   - `_write()` retargets `printf` to both USART1 (blocking HAL) and USBX CDC (non-blocking)
   - CDC transmit via `ux_device_class_cdc_acm_write_run()` with state machine
   - `\r\n` line endings enforced
5. Add startup print: `printf("=== H562 Datalogger Boot ===\r\nSYSCLK: 250 MHz\r\nSPI1: 12.5 MHz\r\nSPI2: 10.0 MHz\r\n")`
6. Modify linker script: heap 16 KB, stack 8 KB

**Key files to create/modify:**
- `Core/Src/debug_uart.c` / `Core/Inc/debug_uart.h` — new files (CubeMX-safe)
- `USBX/App/app_usbx.c` — fix `_ux_utility_time_get()` (USER CODE section)
- `USBX/App/ux_device_cdc_acm.c` — store CDC instance pointer in `USBD_CDC_ACM_Activate` callback
- `Core/Src/main.c` — USER CODE 2 (DCD init) and USER CODE 3 (polling loop)
- `STM32H562RGTX_FLASH.ld` — heap/stack sizes

**Success Criteria:**
- [ ] Build compiles without errors or warnings
- [ ] Windows Device Manager shows USB COM port (VID 0x0483 / PID 0x5740)
- [ ] PuTTY/TeraTerm shows startup message over USB CDC
- [ ] USART1 (PA9/PA10) simultaneously shows same output at 921600 baud
- [ ] `printf("%.3f\r\n", 3.14159f)` prints `3.142` with correct decimal and CRLF
- [ ] `\033[2J` from firmware clears terminal screen

---

### Phase 2 — VT220 Debug UI
**Goal:** Live status panel in terminal; copy-pastable status report.

**Actions:**
1. Implement `debug_ui.c/.h` with ANSI escape sequences
2. Static panel rows 1–17 drawn on startup
3. Field update functions using `\033[row;colH` cursor positioning
4. `ui_log(fmt, ...)` function for scrolling debug output below row 19
5. `ui_print_report()` prints flat key=value block

**Status Panel:**
```
Row  1: ╔══════════════════════════════════════════════════════╗
Row  2: ║  H562 PARACHUTE DATALOGGER    v1.0    STATE: IDLE    ║
Row  3: ╠══════════════════════════════════════════════════════╣
Row  4: ║  ADC                                                 ║
Row  5: ║    CLKIN    :  8192000 Hz  (target 8192000)          ║
Row  6: ║    DRDY     :    64000 Hz  (target 64000)            ║
Row  7: ║    Force    :   +12.345 N                            ║
Row  8: ╠══════════════════════════════════════════════════════╣
Row  9: ║  IMU  (LSM6DSV @ 500 Hz)                            ║
Row 10: ║    Accel   X: +0.000  Y: +0.000  Z: +9.810  m/s²   ║
Row 11: ║    Gyro    X: +0.000  Y: +0.000  Z: +0.000  dps    ║
Row 12: ╠══════════════════════════════════════════════════════╣
Row 13: ║  SYSTEM                                              ║
Row 14: ║    Vbat: 3.72V (65%)  CHG: BATTERY   USB: ---       ║
Row 15: ║    SD: READY   Cal: SD-FILE                         ║
Row 16: ║    Samples:   500000   Overflows: 0                 ║
Row 17: ║    Written:  16.0 MB   Elapsed: 00:16:40            ║
Row 18: ╚══════════════════════════════════════════════════════╝
```

**Copy-Pastable Report (`r` keypress or every 60 s):**
```
=== DATALOGGER STATUS REPORT 2026-04-11 14:23:05 ===
state            : LOGGING
clkin_hz         : 8192000
drdy_hz          : 64000
force_N          : +12.345
accel_x_mss      : +0.001
accel_y_mss      : +0.002
accel_z_mss      : +9.810
gyro_x_dps       : +0.001
gyro_y_dps       : -0.002
gyro_z_dps       : +0.000
vbat_v           : 3.72
soc_percent      : 65
charge_state     : BATTERY
usb_connected    : NO
samples          : 500000
overflows        : 0
sd_written_mb    : 16.0
session_elapsed_s: 1000
cal_source       : SD_FILE
sensitivity_uVpN : 2.000000
======================================================
```

**Success Criteria:**
- [ ] Terminal shows static panel with box-drawing characters (not garbled)
- [ ] Individual field updates don't cause flicker or redraw the whole screen
- [ ] Debug log scrolls naturally below row 19
- [ ] `r` keypress outputs flat key=value block — can be copy-pasted into Claude/Cursor
- [ ] Panel updates ≤ 10 Hz for ADC/IMU fields, ≤ 1 Hz for system fields

---

### Phase 3 — USB DFU Bootloader ⏭ SKIPPED
**Goal:** Firmware can be uploaded over USB without STLink.

**Status:** Skipped — hardware BOOT0 button provides DFU entry without firmware support. PC13-based jump did not work reliably.

---

### Phase 4 — FatFS SD Card ✅ COMPLETE
**Goal:** FAT-formatted SD card writable from firmware and readable on PC.

**Actions completed:**
1. FatFS + `sd_diskio.c` bridging to HAL SDMMC via DMA (`HAL_SD_ReadBlocks_DMA` / `HAL_SD_WriteBlocks_DMA`)
2. `MX_FATFS_Init()` called in `main.c` USER CODE 2
3. TEST.TXT written on boot, readable on PC
4. GPIO pull-ups and speed overrides for SDMMC data/CMD lines in `sdmmc.c` MspInit
5. SDMMC1 NVIC priority set to 5 after `HAL_SD_MspInit` (which resets it to 0)
6. USB keepalive (`ux_system_tasks_run()` + `cdc_poll()`) during SD operations to prevent CDC de-enumeration
7. USB product string changed to "ValueXT Loadcell Datalogger"

**Key discovery: polling SD transfers fail on STM32H5.** `HAL_SD_ReadBlocks` / `HAL_SD_WriteBlocks` return `DATA_TIMEOUT` (error 0x20) in 4-bit mode. DMA variants (`_DMA`) work correctly. This is mandatory — not an optimization.

**SD Card Benchmark Results (64 GB SanDisk, FAT32):**

| Clock | Chunk | Throughput | Worst Write | Stalls ≥50ms |
|-------|-------|-----------|-------------|-------------|
| 25.0 MHz | 512 B | 317 KB/s | 38 ms | 0 |
| 25.0 MHz | 2048 B | 326 KB/s | 46 ms | 0 |
| 25.0 MHz | 4096 B | 322 KB/s | 49 ms | 0 |
| 12.5 MHz | 512 B | 309 KB/s | 40 ms | 0 |
| 12.5 MHz | 2048 B | 317 KB/s | 45 ms | 0 |

**Production settings:** CLKDIV=1 (25 MHz), 2 KB flush chunks (best worst-case latency).

**Max logging rate sweep (8B ADC records, 10s per rate):**

| Rate | Throughput | Result |
|------|-----------|--------|
| 32,000 SPS | 250 KB/s | PASS (0 drops) |
| 33,000 SPS | 257 KB/s | FAIL (359 drops) |
| **Absolute max: 32,000 SPS (250 KB/s)** | | |
| **Derated (-25%): 24,000 SPS (187 KB/s)** | | |

**60-second endurance tests (zero drops):**

| Scenario | Rate | Worst Write | Data Written |
|----------|------|-------------|-------------|
| 500 SPS × 32B `log_record_t` | 15.6 KB/s | 9 ms | 0.92 MB |
| 24k ADC + 500 IMU (combined) | 203 KB/s | 41 ms | 11.9 MB |

**Conclusion:** 64 kSPS raw ADC logging is not feasible (needs 500 KB/s, card ceiling is 250 KB/s). **Selected architecture:** dual-file logging with two-stage decimation — 8 kSPS binary ADC records (128 KB/s) + 500 SPS binary Force+IMU records (16 KB/s) + 500 SPS framed CSV (11 KB/s) = ~155 KB/s total (62% of ceiling). See "Dual-File Logging Format" section for full spec.

**Success Criteria:**
- [x] `f_mount()` returns `FR_OK`
- [x] `f_open("0:TEST.TXT", ...)` returns `FR_OK`
- [x] `f_write()` writes "Hello FatFS\r\n", `f_close()` returns `FR_OK`
- [x] SD card inserted into PC shows `TEST.TXT` with correct content
- [x] Serial terminal shows: `SD: mounted OK, free=59613 MB` on boot
- [x] Serial terminal shows: `SD: init failed, card state=0` if no card inserted
- [x] USB CDC remains enumerated during SD operations
- [x] 500 SPS × 32B logging sustained 60s with zero drops
- [x] 24k SPS + 500 IMU logging sustained 60s with zero drops

---

### Phase 5 — LTC6903 Oscillator ✅ COMPLETE
**Goal:** ADS131M02 CLKIN is 8.192 MHz ± 0.5%.

**Actions:**
1. Implement `osc_ltc6903.c`: SPI1 Mode 0 write, frequency word calculation
2. Write `ltc6903_init()` — called in main before EXTI2 enabled
3. Start TIM8 input capture on PC6 to measure CLKIN
4. Read captured frequency in main loop, display on VT220 UI

**SPI Mode switch (Mode 1 → Mode 0 → Mode 1):**
```c
// In ltc6903_init() only, before EXTI2 is enabled
__HAL_SPI_DISABLE(&hspi1);
hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;  // Temporarily Mode 0
HAL_SPI_Init(&hspi1);
__HAL_SPI_ENABLE(&hspi1);
// ... write LTC6903 ...
__HAL_SPI_DISABLE(&hspi1);
hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;  // Restore Mode 1 for ADS
HAL_SPI_Init(&hspi1);
__HAL_SPI_ENABLE(&hspi1);
```

For 8.192 MHz: LTC6903 word = `(OCT << 12) | (DAC << 2) | 0x00`.
Compute OCT/DAC with brute-force search, verify against datasheet table.

**Success Criteria:**
- [x] TIM8 external clock measures **8.192 MHz ± 0.5%** on PC6 (DWT-precision measurement confirms ~0.03% after auto-trim)
- [x] `ui_log()` shows CLKIN field as `8192xxx Hz` at boot and VT220 panel updates
- [x] Frequency stable over 60 s observation (spread ~4700 Hz = real oscillator drift, not measurement noise)
- [x] Serial terminal shows: `LTC6903: init OK, word=0xCF60 (OCT=12 DAC=984 CNF=0)`
- [x] Serial terminal shows: `LTC6903: SPI mode switch Mode0->Mode1 complete`
- [x] **BONUS:** Boot-time DAC auto-trim converges within ~12 s to <0.05% of target
- [x] **BONUS:** DWT-based nanosecond-precision CLKIN measurement API (`diag_clkin_measure_hz()`)

---

### Phase 6 — ADS131M02 Basic Communication ✅ COMPLETE
**Goal:** Can read/write ADS131M02 registers reliably over SPI1 at 12.5 MHz.

**Actions:**
1. Created `Core/Inc/adc_ads131m02.h` — register defines, command opcodes, address encoding (`addr << 7` per TI/datasheet), configuration constants
2. Created `Core/Src/adc_ads131m02.c` — blocking 12-byte SPI frame helper, two-frame register read, write-with-verify, hardware reset (PA3), full init sequence
3. Init sequence: HW reset → NULL (check STATUS RESET bit) → UNLOCK → ID verify → write MODE (0x0110) → write CLOCK (0x0322, 64 kSPS) → write GAIN1 (0x0000, 1x) → readback verify all
4. Integrated `ads131m02_init()` in `main.c` between `ltc6903_auto_trim()` and `MX_USBX_Init()`
5. Reference context file: `References/ADS131M02_CONTEXT.md`

**ADS131M02 SPI frame (12 bytes, 4×24-bit words):**
```
TX: [CMD_HI CMD_MID CMD_LO] [0x00 0x00 0x00] [0x00 0x00 0x00] [0x00 0x00 0x00]
RX: [STATUS]                [CH0 24-bit]      [CH1 24-bit]      [CRC 24-bit]
```

**Success Criteria:**
- [x] ID register read returns 0x2205 (upper byte 0x22 = ADS131M02, lower byte = silicon rev)
- [x] STATUS register shows proper flags after init (0x0103: DRDY0+DRDY1 set, 24-bit words)
- [x] Write then read back GAIN, MODE, CLOCK registers all return the written values
- [x] No SPI framing errors
- [x] Serial terminal shows: `ADS131M02: init OK, ID=0x2205, STATUS=0x0103`
- [x] Serial terminal shows: `ADS131M02: MODE=0x0110 CLOCK=0x0322 GAIN=0x0000`

---

### Phase 7 — ADS131M02 Continuous 64 ksps Sampling (Zero Loss) ✅ COMPLETE
**Goal:** DRDY fires at 64 kHz; every DRDY triggers a DMA read; zero samples lost.

**Actions:**
1. DMA buffers — plain RAM is fine (STM32H562 Cortex-M33 has **no D-cache**, only ICACHE):
```c
static uint8_t ads_tx_dma[12];
static uint8_t ads_rx_dma[12];
```
No `.dma_buffer` section, no MPU region, no cache maintenance needed.

2. Implement EXTI2 ISR (in `stm32h5xx_it.c` USER CODE section or via HAL callback):
```c
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == ADC_DRDY_Pin) {
        ads131m02_start_dma_read();  // Assert CS + start SPI1 DMA
    }
}
```

3. Implement DMA complete callback (SPI1 TxRx complete):
```c
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance == SPI1) {
        ads131m02_dma_complete();    // Deassert CS + extract data
    }
}
```

4. Implement software DRDY counter: increment on each DRDY EXTI, compute frequency over 1 s.

5. Implement miss counter: if EXTI fires while DMA still in progress (HAL SPI state ≠ READY), increment miss counter.

**Success Criteria (all verified on hardware at 250 MHz):**
- [x] DRDY counter = **63939–64004 Hz** over 1 s windows (HW TIM3 = SW EXTI2) ✓
- [x] DMA complete counter equals DRDY counter (zero misses) ✓
- [x] Miss counter = **0** continuously for 60 s (3,877,727 DRDY, 0 miss) ✓
- [x] Polarity self-test: CH0/CH1 ~±1.08M counts, symmetry error < 0.25% ✓
- [x] DRDY_FMT A/B test: pulse mode = 0 misses, level mode = expected miss pattern ✓
- [x] DMA error count = **0** (stale NVIC pending bug fixed) ✓
- [x] VT220 UI shows live DRDY rate, ADC counts, voltage, SD status, USB status ✓
- [x] Serial terminal: `ADC: sw=63980 hw=63980 ok=63979 miss=0 err=0` ✓
- [x] All above verified with USB CDC active, SD card mounted, ICACHE enabled ✓

**Phase 7 Troubleshooting Log**

Phase 7 was the hardest phase so far — 8 test runs across 2 debugging sessions before reaching zero-loss operation. Full details in the [Troubleshooting/](Troubleshooting/) folder; key issues and resolutions summarized below.

*Bug 1 — HAL DMA overhead (Test Run 1).* `HAL_SPI_TransmitReceive_DMA()` took ~47 µs per call on STM32H5 GPDMA, far exceeding the 15.625 µs DRDY period. Result: 75% miss rate. **Fix:** Replaced with register-level GPDMA restarts (~3 µs total ISR overhead). See [PHASE7_DMA_CRASH_CONTEXT.md §6](Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md).

*Bug 2 — Stale SPI RX FIFO (Test Runs 2–4).* After Phase 6 blocking `HAL_SPI_TransmitReceive()` calls, leftover data sat in SPI1's RX FIFO. `CLEAR_BIT(SPE)` when SPE was already 0 did **not** flush FIFOs (RM0481 requires a 1→0 transition). When DMA started, the stale RXP=1 triggered an immediate spurious RX DMA request, corrupting TSIZE and stalling the transfer. **Fix:** Force-flush FIFOs with `SET_BIT(SPE); CLEAR_BIT(SPE)` in `ads_dma_setup()`. See [PHASE7_DMA_CRASH_CONTEXT.md §15](Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md).

*Bug 3 — HAL processing register-level DMA interrupts (Test Run 5).* TX DMA channel (GPDMA1_CH0) had NVIC enabled from CubeMX. When register-level TX DMA completed, `HAL_DMA_IRQHandler` processed the flags with stale HAL state, corrupting the SPI/DMA pipeline → HardFault under DRDY load. **Fix:** Intercept both GPDMA channels in `stm32h5xx_it.c` with unconditional flag-clear-and-return; disable CH0 NVIC entirely since TX completion needs no interrupt. See [PHASE7_DMA_CRASH_CONTEXT.md §17](Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md).

*Bug 4 — ISR/polling race in manual DMA test (Test Run 6).* `ads_manual_dma_test()` polled for TCF in a loop, but GPDMA1_CH1 NVIC was enabled — the ISR consumed TCF before the polling loop saw it. Appeared as intermittent first-call failure (worked on 3rd attempt by timing luck). **Fix:** Disable CH1 NVIC during manual test, poll TCF directly, re-enable after. Use DWT CYCCNT timeout instead of HAL_GetTick (immune to SysTick starvation at 128K IRQ/sec). See [PHASE7_DMA_CRASH_CONTEXT.md §18–19](Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md).

*Bug 5 — CFG1 written while SPE=1 (Test Run 7).* `ads_fast_complete()` cleared RXDMAEN/TXDMAEN in SPI1->CFG1 before clearing SPE. Per RM0481, CFG1 writes are silently ignored when SPE=1. RXDMAEN/TXDMAEN remained set, so the next DRDY-triggered `ads_fast_start()` saw stale RX FIFO data via the still-enabled DMA path. **Fix:** Reorder shutdown: `SPE=0` first (flushes FIFOs), then clear CFG1. Added hard SPI+DMA cleanup between manual test and DRDY enable. See [PHASE7_DMA_CRASH_CONTEXT.md §20](Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md).

*Bug 6 — GPDMA CSAR/CDAR auto-advance (Test Run 8).* After a NORMAL-mode DMA transfer, STM32H5 GPDMA updates CSAR/CDAR to the *next* address that *would have followed*. `ads_fast_start()` never reloaded these → RX wrote past the buffer (silent RAM corruption), TX read from the wrong buffer. **Fix:** Reload `dma_rx->CDAR` and `dma_tx->CSAR` in `ads_fast_start()` every transfer (+2 register writes, ~80 ns overhead). See [PHASE7_DMA_CRASH_CONTEXT.md §21](Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md).

*Bug 7 — Stale NVIC pending after manual DMA test (post-completion).* `ads_manual_dma_test()` re-enabled GPDMA1_CH1 NVIC after `ads_fast_complete()` cleared TCF in the channel's flag register, but the NVIC pending bit (latched when TCF originally fired) was not cleared. First DRDY-triggered DMA complete saw no TCF → counted as `dma_error_count=1`. **Fix:** Added `NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn)` between `ads_fast_complete()` and `HAL_NVIC_EnableIRQ()`. See [adc_ads131m02.c](Core/Src/adc_ads131m02.c).

*Key design input — external review.* [two_cents.md](Troubleshooting/two_cents.md) provided an independent driver architecture recommendation: DRDY pulse mode (not level), TXC wait + guard NOPs before CS deassert, `ads_spi_force_clean_idle()` pattern for SPE flush, ring buffer skeleton, and the critical STM32H5 SPI rule that RX DMA TC ≠ "SPI bus finished." These insights directly shaped the final driver design.

**SYSCLK Upgrade: 75 MHz → 250 MHz**

After Phase 7 was functionally complete, SYSCLK was upgraded from 75 MHz (VOS3) to 250 MHz (VOS0) to increase ISR headroom from ~8 µs to ~14 µs per DRDY period. Plan: [sysclk_75_to_250_mhz.plan.md](.cursor/plans/sysclk_75_to_250_mhz_2fdbd570.plan.md). Changes: PLL1 N=31/FRACN=2048/Q=10 (VCO 500 MHz, PLL1Q=50 MHz unchanged), VOS0, FLASH_LATENCY_5, PROGRAMMING_DELAY_2, ICACHE enabled, ADC prescaler DIV4. Silicon REV_ID=0x1007 confirmed not affected by errata ES0565 §2.2.18. Boot diagnostics print full clock tree, SPI1 CFG1 readback, USART1 BRR, and ICACHE status. All peripherals verified: SPI1 12.5 MHz SCK, USB CDC, SD 59 GB, UART 921600 baud. See [Serisl_Debug.txt](Troubleshooting/Serisl_Debug.txt) for final hardware output.

**Final state:** [ADS131M02_DRIVER_STATE.md](Troubleshooting/ADS131M02_DRIVER_STATE.md) documents the complete driver architecture, register layouts, ISR wiring, and hot-path code flow as of Phase 7 completion.

---

### Phase 8 — LSM6DSV IMU ✅ COMPLETE
**Goal:** IMU communicates over SPI2, WHO_AM_I correct, accel Z shows gravity, orientation-aware quaternion output.

**Actions completed:**
1. Used existing `Drivers/BSP/Components/lsm6dsv/` driver (register-level access)
2. Implemented `imu_lsm6dsv.c`: init (WHO_AM_I verify, SW_RESET, HA01 2000 Hz ODR, FS=±16g/±2000dps)
3. Read 14 bytes (accel XYZ + gyro XYZ) via **blocking** `BSP_SPI2_SendRecv()`
4. No DMA for SPI2 (GPDMA1 CH2/CH3 removed from SPI2 in Phase 0)
5. Display accel/gyro/quaternion/Euler on VT220 UI
6. Boot-time offset calibration (SW, 256-sample average)
7. SFLP game rotation engine enabled with FIFO streaming (DOE-validated drain_max=32)
8. Software quaternion integration as fallback when SFLP FIFO has no data
9. `halfToFloat()` IEEE 754 half-precision decoder with NaN/Inf/range guards
10. `quatToEuler()` with `asinf` clamping for gimbal-lock robustness
11. Orientation-agnostic gravity axis detection at boot
12. SPI error recovery (`spiRecover()`: abort + deinit + reinit on any SPI error)
13. DWT cycle-counter instrumented `imuRead()` for microsecond-level timing diagnostics
14. IMU die temperature reading via `lsm6dsv_temperature_raw_get()`
15. External Python visualizer (`Tools/imu_visualizer.py`) for real-time 3D orientation + telemetry
16. Automated DOE test harness to characterize FIFO drain / SPI speed robustness (completed and stripped)

**Final production settings (DOE-validated):**
- SPI2 baud rate: **10.0 MHz** (prescaler /2 from PLL2P 20 MHz) — LSM6DSV max 10 MHz
- FIFO drain cap: **32 entries** per `imuRead()` call
- HA01 ODR: **2000 Hz** accel + gyro
- SFLP game rotation: enabled, batched to FIFO
- `imuRead()` worst-case: **~230 µs** (avg ~140 µs)

**Success Criteria:**
- [x] WHO_AM_I read = **0x70**
- [x] `accel_z` (converted with sensitivity) = **+9.81 ± 0.5 m/s²** when board is flat (after boot-time calibration)
- [x] `accel_x` and `accel_y` ≈ **0 ± 0.2 m/s²** when board is flat (after boot-time calibration)
- [x] Gyro XYZ ≈ **0 ± 0.5 dps** when stationary (after boot-time calibration)
- [x] VT220 UI IMU fields update at ~10 Hz display rate
- [x] Serial terminal shows: `[IMU] WHO_AM_I=0x70 OK` and `[IMU] HA01 2000Hz OK` at boot
- [x] Serial terminal shows periodic IMU data (accel, gyro, quaternion, Euler, temperature, drift, gravity axis)
- [x] IMU reads verified while ADC DMA is running simultaneously (zero DRDY misses)
- [x] **BONUS:** SFLP hardware quaternion fusion with software fallback
- [x] **BONUS:** Orientation-agnostic calibration (works in any boot orientation)
- [x] **BONUS:** Python 3D visualizer with quaternion-based rotation, live telemetry panels
- [x] **BONUS:** SPI error recovery — zero MCU freezes during board shake/movement
- [x] **BONUS:** DOE-validated production parameters (drain_max=32, SPI2=10 MHz)

**Phase 8 Troubleshooting Log**

Phase 8 went through significant hardening after the initial driver was functional. The core driver worked immediately, but field-robustness issues emerged during motion testing. Full details in the sub-plan files; key issues and resolutions summarized below.

*Bug 1 — MCU freeze during board movement (SFLP FIFO drain).* When the board was physically moved or shaken, the MCU entered a HardFault or hung indefinitely. **Root cause:** The SFLP FIFO drain loop (originally up to 128 entries) saturated the SPI2 bus with back-to-back blocking reads. At high motion rates, the FIFO filled rapidly and the drain consumed the entire main-loop timeslice, starving USB CDC polling and eventually triggering a USB timeout or SPI bus lockup. **Fix:** Conservative FIFO drain cap (initially 8, DOE-validated to 32). Added `spiRecover()` — on any `BSP_SPI2_SendRecv` error, the function calls `HAL_SPI_Abort`, `HAL_SPI_DeInit`, `HAL_SPI_Init` to fully reset the SPI2 peripheral. See [imu_freeze_diagnostic_fix plan](.cursor/plans/imu_freeze_diagnostic_fix_4eede95c.plan.md).

*Bug 2 — halfToFloat() producing NaN/Inf from SFLP FIFO data.* During fast rotation, SFLP FIFO entries occasionally contained half-precision values that decoded to NaN or Inf. These propagated through `sqrtf(1 - x² - y² - z²)` (W derivation) and `asinf()` (Euler pitch), producing NaN in the output. `snprintf("%+.5f", NaN)` on embedded newlib can take excessive time or fault. **Fix:** Added post-decode guards in `halfToFloat()` (`if (r.f != r.f || r.f > 1e6f || r.f < -1e6f) return 0.0f`), clamped `asinf` argument to [-1, 1], added quaternion norm sanity check with fallback to software quaternion on invalid data. See [imu_freeze_diagnostic_fix plan](.cursor/plans/imu_freeze_diagnostic_fix_4eede95c.plan.md).

*Bug 3 — Fault handlers were silent infinite loops.* All four fault handlers (HardFault, MemManage, BusFault, UsageFault) in `stm32h5xx_it.c` were `while(1)` with zero diagnostic output, making crash diagnosis impossible. **Fix:** Instrumented all fault handlers with `fault_dump()` that prints SCB->CFSR, SCB->HFSR, SCB->BFAR, SCB->MMFAR, and stacked LR via direct `HAL_UART_Transmit` (not printf, which could itself be the fault trigger).

*Bug 4 — CDC buffer overflow corrupting visualizer data.* The 2 KB CDC ring buffer was too small for 20 Hz × ~200 bytes/line = 4 KB/s throughput. Partial lines reached the Python visualizer, causing parse failures and apparent "no data" symptoms. **Fix:** Increased `CDC_TX_BUF_SIZE` from 2048 to 4096 bytes. Switched `$IMU` CSV output from `printf` (which adds timestamp prefix) to raw `cdc_write()` for clean framing.

*Bug 5 — HA01 ODR + SFLP compatibility uncertainty.* The HA01 (High Accuracy) ODR mode is a non-standard LSM6DSV feature. It was unknown whether the SFLP sensor fusion engine would function at HA01 rates (500/1000/2000 Hz) since SFLP documentation only references standard ODRs. **Fix:** Implemented an ODR test loop in `imuInit()` that tries HA01 rates from 2000 Hz downward, verifies SFLP produces tagged FIFO samples at each rate, and falls back to 480 Hz standard if all fail. Result: HA01 2000 Hz works with SFLP.

*Design of Experiments (DOE) — SPI speed and FIFO drain optimization.* After resolving the freeze, a systematic DOE was conducted to find optimal production parameters. The automated test harness swept 6 configurations (drain_max × SPI speed) across 4 motion phases (stationary, shake, recovery, tilt/tap) with DWT-instrumented timing and automatic PASS/WARN/FAIL grading. **Result:** `drain_max=32` with SPI2 at 10 MHz achieved zero SPI errors, zero FIFO overflows in steady state, and `imuRead()` averaging ~140 µs. DOE harness was subsequently stripped from production firmware.

**Key sub-plan references:**
- [Phase 8 IMU driver plan](.cursor/plans/phase_8_lsm6dsv_imu_ba810260.plan.md) — original driver architecture and SPI wiring
- [IMU Calibration + SFLP plan](.cursor/plans/imu_calibration_sflp_fb144425.plan.md) — boot-time offset calibration, SFLP game rotation enable, gyro bias seeding
- [IMU SFLP Quaternion/Euler plan](.cursor/plans/imu_sflp_quat_euler_abcb0a06.plan.md) — FIFO setup, half-float decoder, quaternion/Euler readout, HA01 ODR testing, visualizer, VIZ_STREAM
- [IMU Freeze Diagnostic Fix plan](.cursor/plans/imu_freeze_diagnostic_fix_4eede95c.plan.md) — fault handler instrumentation, float guards, SPI recovery, CDC buffer fix

---

### Phase 9a — NeoPixel Status LEDs (WS2812)
**Goal:** Two daisy-chained WS2812 NeoPixels on PA0 (TIM2 CH1 PWM + GPDMA) provide real-time system and subsystem status. LED 0 = system health (power, battery, charging, error). LED 1 = subsystem health + logging indicator (ADC, IMU, SD, Logger).

**Prerequisite:** Phase 8 complete. Must be implemented before Phase 9b (battery monitoring) because NeoPixels are the primary visual diagnostic channel for all subsequent phases.

**Actions:**
1. IOC: add GPDMA1 Channel 2 for TIM2_UP (Memory-to-Peripheral, Word, Normal mode). Verify TIM2 CH1 PWM config (PSC=0, ARR=312). Combined with Phase 9b battery GPIO regen.
2. Create `Core/Inc/neopixel.h` / `Core/Src/neopixel.c` — low-level WS2812 driver: `neoInit()`, `neoSetPixel()`, `neoShow()`, `neoOff()`. DMA buffer encodes GRB bit-periods (NEO_BIT0_DUTY=100, NEO_BIT1_DUTY=200 at 250 MHz TIM clock).
3. Create `Core/Inc/led_status.h` / `Core/Src/led_status.c` — high-level status engine: `ledStatusInit()`, `ledStatusUpdate()` (20 Hz from main loop), `ledStatusSetSys()`, `ledStatusSetSub()`, `ledStatusSetLogging()`. IEC 60073 blink patterns (SOLID, SLOW BLINK 1 Hz, FAST BLINK 4 Hz, HEARTBEAT).
4. Integrate into `main.c`: uncomment `MX_TIM2_Init()`, override TIM2 NVIC to priority 10, call `neoInit()` + `ledStatusInit()` early in USER CODE BEGIN 2. Add `ledStatusSetSub()` calls after each subsystem init. Add 50 ms `ledStatusUpdate()` tick in main loop.
5. Wire `ledStatusSetSub()` into existing ADC, IMU, and SD init paths.

**Customer spec (non-negotiable):** Power ON = RED solid (LED 0), Low battery = ORANGE solid (LED 0), Logging = GREEN solid (LED 1). No yellow. No color reused with same pattern on both LEDs simultaneously.

**Doxygen/naming compliance:** All new files follow `.cursor/rules/commenting-and-naming.mdc` from creation. Any existing file modified (e.g., `main.c`) must have Doxygen added to touched functions/sections per Phase 14 incremental rule.

**Key files:**
- `Core/Src/neopixel.c/.h` — new (CubeMX-safe)
- `Core/Src/led_status.c/.h` — new (CubeMX-safe)
- `Core/Src/main.c` — USER CODE sections edited

**Detailed plan:** [Phase 9a NeoPixel plan](.cursor/plans/phase_9a_neopixel_status_leds.plan.md)

**Success Criteria:**
- [x] Both LEDs light up after `neoInit()` (RED HEARTBEAT on LED 0, OFF on LED 1 during boot)
- [x] LED 0 transitions to RED SOLID after boot complete (`LED_SYS_IDLE`)
- [x] LED 1 shows GREEN HEARTBEAT after all subsystems report OK
- [ ] Subsystem warning/error states produce correct color and blink pattern on LED 1 *(deferred — requires fault injection)*
- [ ] LED 1 rotation engine cycles through multiple warnings with 200 ms OFF gap *(deferred — requires multiple faults)*
- [x] `neoShow()` DMA transfer completes in ~60 us, no contention with SPI1 ADC hot path
- [x] Zero DRDY misses over 60 s with NeoPixel updates running at 20 Hz
- [x] Build compiles with zero warnings
- [x] All new code follows Doxygen and naming conventions per `.cursor/rules/commenting-and-naming.mdc`
- [x] Modified existing code has Doxygen on touched functions

**Status: COMPLETE** (2026-04-12)

---

### Phase 9b — Battery Monitoring, SOC Estimation, Charger Status, MCU Temperature, and USB Sense
**Goal:** Battery voltage and SOC read correctly; BQ24012 charge state displays on VT220 UI; MCU internal temperature sensor read via ADC1; USB connection prevents logging. NeoPixel LED 0 reflects battery/charge state via `ledStatusSetSys()`.

**IOC Changes (Completed 2026-04-12):**
- Disabled I2C1 peripheral (freed PB6)
- PB7 → `GPIO_Input`, Pull-up, label `CHG_PG`
- PB5 → `GPIO_Input`, Pull-up, label `CHG_STAT1`
- PB6 → `GPIO_Input`, Pull-up, label `CHG_STAT2`
- ADC1: enabled Temperature Sensor (VSENSE) and VREFINT internal channels
- ADC2: enabled VDDCORE channel
- Two CubeMX regens performed; all USER CODE sections verified and restored

> PB1 (`USB_SENSE`) is already configured as `GPIO_Input` with pull-down. No IOC change needed.

**Actions:**
1. Implement `battery_monitor.c/.h`:
   - `battery_init()` — configure ADC1 channel for PA1 (battMon), single conversion mode
   - `battery_poll()` — called from main loop every 60 s (use `HAL_GetTick()` delta):
     - Start ADC1 conversion (polling, ~10 µs)
     - Convert raw ADC to voltage: `v_batt = (adc_raw / 4096.0f) * 3.3f / batt_divider_ratio`
     - Look up SOC from voltage (see table below)
     - Read PG, STAT1, STAT2 GPIOs → decode `charge_state_t`
     - Cache all values for UI and log record
   - `battery_get_voltage()` — returns cached float
   - `battery_get_soc_percent()` — returns cached uint8_t (0–100)
   - `battery_get_charge_state()` — returns cached enum
   - `battery_is_usb_connected()` — reads PB1 (USB_SENSE), returns bool
   - `batt_get_mcu_temp_x10()` — returns cached `int16_t` (tenths of degree C, e.g. 235 = 23.5 C)

2. **MCU internal temperature sensor (ADC1 VSENSE):**

   The STM32H562 has an internal temperature sensor on ADC1 channel VSENSE. Add temperature reading to the same ADC1 polling cycle as battery voltage:

   - Configure ADC1 to also convert the VSENSE channel (in addition to PA1 battMon)
   - Read once per second (alongside battery voltage reading)
   - Apply factory calibration from `TEMPSENSOR_CAL1_ADDR` and `TEMPSENSOR_CAL2_ADDR`:
     ```c
     int16_t batt_get_mcu_temp_x10(void)
     {
         uint16_t cal1 = *TEMPSENSOR_CAL1_ADDR;  // ~30 C calibration
         uint16_t cal2 = *TEMPSENSOR_CAL2_ADDR;  // ~130 C calibration
         int32_t temp = 300 + (1300 - 300) * (int32_t)(adc_raw - cal1) / (int32_t)(cal2 - cal1);
         return (int16_t)temp;  // tenths of degree C
     }
     ```
   - Cache value for metadata record (Phase 11) and VT220 UI display

3. **SOC estimation — voltage-based lookup table:**

   Coulomb counting is not feasible (no current sense resistor). Voltage-based SOC is the practical approach for a single Li-ion cell (BQ24012 charges to 4.2V, typical cutoff 3.0V).

   The Li-ion OCV (Open Circuit Voltage) vs SOC curve is nonlinear. Under load, voltage sags — but at 60 s polling intervals the cell is essentially at rest between polls for this purpose.

   **Piecewise linear lookup table (typical Li-ion, 1S):**
   ```c
   /* OCV vs SOC lookup — 11 points, piecewise linear interpolation */
   typedef struct { float voltage; uint8_t soc; } soc_point_t;

   static const soc_point_t soc_table[] = {
       { 4.20f, 100 },
       { 4.10f,  90 },
       { 4.00f,  80 },
       { 3.90f,  70 },
       { 3.80f,  60 },
       { 3.70f,  50 },
       { 3.60f,  35 },
       { 3.50f,  20 },
       { 3.40f,  10 },
       { 3.30f,   5 },
       { 3.00f,   0 },
   };
   #define SOC_TABLE_LEN  (sizeof(soc_table) / sizeof(soc_table[0]))

   uint8_t battery_voltage_to_soc(float voltage)
   {
       if (voltage >= soc_table[0].voltage) return 100;
       if (voltage <= soc_table[SOC_TABLE_LEN - 1].voltage) return 0;

       for (uint32_t i = 0; i < SOC_TABLE_LEN - 1; i++) {
           if (voltage >= soc_table[i + 1].voltage) {
               /* Linear interpolation between points */
               float v_range = soc_table[i].voltage - soc_table[i + 1].voltage;
               float s_range = (float)(soc_table[i].soc - soc_table[i + 1].soc);
               float frac = (voltage - soc_table[i + 1].voltage) / v_range;
               return soc_table[i + 1].soc + (uint8_t)(frac * s_range);
           }
       }
       return 0;
   }
   ```

   **SOC accuracy note:** Voltage-based SOC is ±10% accurate in the flat region (3.6–3.9V) where voltage changes slowly. It's most accurate near full (>4.0V) and near empty (<3.4V) where the curve is steep. This is acceptable for a datalogger — the user needs to know "battery is low, stop soon" not precise Wh remaining.

   **Override when charging:** If `charge_state == CHG_CHARGING`, the voltage is elevated by charge current. Display SOC as `"--% (charging)"` on the UI to avoid misleading readings. If `charge_state == CHG_FULL`, display `100%`.

4. **Charge state decode:**
   ```c
   typedef enum {
       CHG_BATTERY,    /* No input power — running on battery */
       CHG_CHARGING,   /* Input power present, charging */
       CHG_FULL,       /* Input power present, charge complete */
       CHG_STANDBY,    /* Input power present, standby or fault */
   } charge_state_t;

   charge_state_t battery_get_charge_state(void)
   {
       if (HAL_GPIO_ReadPin(CHG_PG_GPIO_Port, CHG_PG_Pin) == GPIO_PIN_SET)
           return CHG_BATTERY;

       uint8_t s1 = (HAL_GPIO_ReadPin(CHG_STAT1_GPIO_Port, CHG_STAT1_Pin) == GPIO_PIN_RESET);
       uint8_t s2 = (HAL_GPIO_ReadPin(CHG_STAT2_GPIO_Port, CHG_STAT2_Pin) == GPIO_PIN_RESET);

       if (s1 && !s2)  return CHG_CHARGING;
       if (!s1 && s2)   return CHG_FULL;
       return CHG_STANDBY;
   }

   static const char *charge_state_str[] = {
       "BATTERY", "CHARGING", "FULL", "STANDBY"
   };
   ```

5. **USB_SENSE (PB1) — logging gate:**
   ```c
   bool battery_is_usb_connected(void)
   {
       /* PB1 has pull-down. USB VBUS present → HIGH. */
       return (HAL_GPIO_ReadPin(USB_SENSE_GPIO_Port, USB_SENSE_Pin) == GPIO_PIN_SET);
   }
   ```

   **Integration with state machine (`app_state.c`):**
   - **Dev mode** (`allow_log_on_usb = 1` in config.txt): logging allowed regardless of USB state. This is needed during development for bench testing with USB debug connected.
   - **Production mode** (`allow_log_on_usb = 0`, default): on `logStart` button press, check `battery_is_usb_connected()`. If true, reject with `ui_log("Cannot start logging: USB connected. Set allow_log_on_usb=1 to override.")`. Stay in IDLE.
   - While LOGGING: never auto-stop if USB is plugged in mid-session — only `logStart` button or error stops logging.
   - VT220 UI: show `USB: CONNECTED` / `USB: ---` in system section.

6. Update VT220 UI:
   - Row 14: `Vbat: 3.72V (65%)  CHG: BATTERY  USB: ---`
   - Status report: add `soc_percent`, `charge_state`, `usb_connected` fields

7. **NeoPixel LED 0 integration** (Phase 9a provides the driver):
   - In `batteryPoll()`: call `ledStatusSetSys(LED_SYS_BATT_LOW)` when SOC <= 20%, `ledStatusSetSys(LED_SYS_BATT_CRIT)` when SOC <= 5%, `ledStatusSetSys(LED_SYS_CHARGING)` when charge state is CHG_CHARGING, `ledStatusSetSys(LED_SYS_IDLE)` when on battery and SOC > 20%
   - Provides real-time visual feedback on LED 0 for battery/charge state — customer-facing

**Doxygen/naming compliance:** All new files follow `.cursor/rules/commenting-and-naming.mdc` from creation. Any existing file modified must have Doxygen added to touched functions/sections per Phase 14 incremental rule.

**Key files:**
- `Core/Src/battery_monitor.c` / `Core/Inc/battery_monitor.h` — new (CubeMX-safe)
- `Core/Src/app_state.c` — add USB check in IDLE→LOGGING transition
- `Core/Src/gpio.c` — CubeMX regenerated with new pin labels (PC14, PC15, PB6)
- `Core/Src/debug_ui.c` — add SOC, charge state, USB status to panel
- `Core/Src/led_status.c` — receives `ledStatusSetSys()` calls from battery monitor (Phase 9a dependency)

**Pin assignments:**

| Signal | Source | MCU Pin | Pull | Notes |
|--------|--------|---------|------|-------|
| battMon | Resistor divider | PA1 (ADC1_INP1) | — | Analog input, 8x oversampled |
| PG | BQ24012 (active-low OD) | PB7 | Pull-up | HIGH=no input power, LOW=power good |
| STAT1 | BQ24012 (push-pull) | PB5 | Pull-up | See decode table in BQ24012 section above |
| STAT2 | BQ24012 (push-pull) | PB6 | Pull-up | I2C1 disabled to reclaim pin |
| USB_SENSE | VBUS divider | PB1 | Pull-down | HIGH=USB connected, already configured |

**Success Criteria:**
- [x] `batteryGetVoltage()` returns realistic value (3.0–4.2 V for Li-ion) — verified ~2.1V with no battery (floating ADC), correct behavior
- [x] `batteryGetSocPercent()` returns 0-100 via OCV lookup; 0% when no battery, piecewise-linear interpolation verified
- [x] SOC display shows `---` while charging (OCV unreliable under charge current)
- [ ] Plugging in USB: charge state changes from `BATTERY` to `CHARGING` *(deferred — no charger board connected yet)*
- [ ] After full charge: charge state shows `FULL` *(deferred — no charger board connected yet)*
- [ ] Unplugging USB: charge state returns to `BATTERY` *(deferred — no charger board connected yet)*
- [ ] `batteryIsUsbConnected()` returns true when USB cable plugged in *(deferred — USB_SENSE trace not yet validated)*
- [ ] With `allow_log_on_usb=0`: pressing `logStart` while USB connected → logging rejected *(deferred — Phase 11 state machine)*
- [ ] With `allow_log_on_usb=1`: pressing `logStart` while USB connected → logging starts *(deferred — Phase 11)*
- [ ] With USB disconnected: logging always starts regardless of config setting *(deferred — Phase 11)*
- [x] VT220 UI shows voltage, SOC%, charge state, USB status, MCU temperature
- [ ] Status report includes `vbat_v`, `soc_percent`, `charge_state`, `usb_connected` fields *(deferred — Phase 11 logging)*
- [x] `battGetMcuTempX10()` returns realistic value — verified 35–43 C at 250 MHz (V_30=620mV, Slope=2.0mV/C)
- [x] MCU temperature updates once per second on VT220 UI
- [x] Serial terminal shows battery, SOC, charger, USB, MCU temp, VDDA, VCORE, GPIO states at 1 Hz
- [x] ADC1 conversion (battery + temperature + VREFINT) does not interfere with SPI1 DMA — zero DRDY misses confirmed
- [x] LED 0 shows ORANGE FAST BLINK (BCRT) when SOC <= 5% — confirmed with no battery
- [x] LED 0 battery/charge state integration via `ledStatusSetSys()` working
- [ ] LED 0 shows ORANGE SOLID when SOC <= 20% *(deferred — needs battery at 3.3-3.5V)*
- [ ] LED 0 shows BLUE SLOW BLINK when charging *(deferred — needs charger board)*
- [ ] LED 0 returns to RED SOLID when on battery with SOC > 20% *(deferred — needs battery)*
- [x] All new and modified code follows Doxygen and naming conventions per `.cursor/rules/commenting-and-naming.mdc`
- [x] VDDA measured via VREFINT: 3273–3293 mV (stable, nominal 3.3V)
- [x] VDDCORE measured via ADC2: 1347–1361 mV (healthy for VOS0 at 250 MHz)
- [x] Battery ADC 8x oversampling reduces noise spread from ~0.23V to ~0.14V

**Status: COMPLETE** (2026-04-12) — core firmware verified, hardware-dependent items deferred until charger/battery connected.

**Detailed plan:** [Phase 9b Battery Monitoring plan](.cursor/plans/phase_9b_battery_usb_sense.plan.md)

---

### Phase 10 — Two-Stage Decimation, Force Calculation, and Record Assembly
**Goal:** Two-stage boxcar decimation produces 8 kHz ADC records and 500 Hz Force+IMU records; ratiometric force output reads correct Newtons with known calibration weights.

**Note:** Initial Phase 10 used **`config.txt`** on **`0:`** for calibration. **Phase 10b** supersedes that with binary **`.cal`** on **`1:`**, `cellCorrFactor`, `CH1_DIV_RATIO`, and boot-time cell selection — see [Phase 10b](phase_10b_cal_sd_partitions_751257fe.plan.md). The decimation/ISR design below still applies.

**Actions:**
1. Create `Core/Inc/log_record.h` with all packed struct definitions:
   - `bin_file_header_t` (64 bytes)
   - `bin_adc_record_t` (16 bytes, type 0x01)
   - `bin_force_record_t` (32 bytes, type 0x02)
   - `bin_meta_record_t` (32 bytes, type 0x03)
   - Validity flag defines
   - CRC16-CCITT helper

2. Implement two-stage boxcar decimation in the DMA-complete ISR (`data_processing.c/.h`):
   - **Stage 1 (every 8 DRDYs = 8 kHz):** Sum 8 raw CH1/CH2 values. Assemble and emit a `bin_adc_record_t` (raw 8-sample sum, not divided). Reset partial accumulator.
   - **Stage 2 (every 128 DRDYs = 500 Hz):** Sum of 128 raw values (= 16 stage-1 sums). Compute `force_N`, read IMU via blocking SPI2. Assemble `bin_force_record_t`. Format CSV line `$,time_ms,load_N,#\r\n`.
   - Both stages write records to a staging area; the ring buffer push happens in Phase 11.

3. At 500 Hz decimation boundary compute force via ratiometric bridge equation:
   `force_N = (acc_ch0 / acc_ch1) * (V_exc * CH1_DIV_RATIO * 1e6 / (sensitivity_uV_per_N * cellCorrFactor)) - tare_offset_N`
   - `acc_ch0 / acc_ch1` ≈ `V_bridge / V_ch1_actual` (ADC ratio cancels gain/drift)
   - `V_exc = 3.3 V` (supply rail)
   - `CH1_DIV_RATIO = 33 / 133` (R6/(R3+R6) — CH1 sees divided excitation, not full 3.3 V)
   - `1e6` converts sensitivity from µV/N to V/N
   - Full derivation: [Phase 10b plan, Section 5](/.cursor/plans/phase_10b_cal_sd_partitions_751257fe.plan.md)

4. Load calibration: **Phase 10 (historical)** used `config.txt` on `0:` with keys such as `sensitivityUvPerN`, gains, etc. **Phase 10b (current):** factory **`1:<serial>.cal`** (64-byte packed blob, CRC16), VT220 cell pick, `calibrationLoadFromCal()`, `cellCorrFactor`, and `write_cal.py` using the **same table-driven CRC** as `log_record.h`. See [Phase 10b](phase_10b_cal_sd_partitions_751257fe.plan.md).
> `allowLogOnUsb` in `.cal` = 1 during development; 0 for production if logging must be blocked when USB is present.

5. Implement tare function (store zero reading, subtract)

6. `pending_adc_record` flag set every 8 DRDYs; `pending_force_record` flag set every 128 DRDYs; both cleared by main loop

**Key files:**
- `Core/Inc/log_record.h` — new: all packed record type structs and CRC helper
- `Core/Src/data_processing.c` / `Core/Inc/data_processing.h` — new: decimation logic, record assembly
- `Core/Src/calibration.c` / `Core/Inc/calibration.h` — new: config.txt parser *(Phase 10b: binary `.cal` reader — see Phase 10b plan)*

**Success Criteria:**
- [ ] `bin_adc_record_t` emitted exactly **8000 times/second** (measure over 10 s)
- [ ] `bin_force_record_t` emitted exactly **500 times/second** (measure over 10 s)
- [ ] Decimation ratio verified: every force record contains the sum of exactly 128 raw samples (= 16 ADC records)
- [ ] Unloaded loadcell shows force approximately **0 N** after tare
- [ ] Known calibration weight (e.g., 1 kg = 9.81 N) reads **9.81 +/- 0.5 N**
- [ ] *(Phase 10b)* Changing `.cal` / cell selection updates force scaling after reboot (no `config.txt` requirement)
- [ ] VT220 UI Force field updates at 10 Hz or less showing stable reading
- [ ] *(Phase 10b)* Serial shows successful `.cal` load / `SN:` / or fault — not the legacy `CAL: loaded from SD, sensitivity=2.000` line unless using old config path
- [ ] Serial terminal shows (periodic): `DECIM: ADC=8000/s FORCE=500/s`
- [ ] CSV line format verified: `$,<time_ms>,<load_N>,#` with correct framing
- [ ] All above verified with zero DRDY misses over 60 s

---

### Phase 10b — Dual-Partition SD, Binary Calibration Files, and Cell Selection
**Goal:** Non-technical users select a pre-calibrated load cell on every boot via the VT220 UI. Factory-written binary `.cal` files on the SYSCAL volume provide per-cell calibration with CRC16 integrity. No user ever opens, edits, or interacts with any configuration file.

**Prerequisite:** Phase 10 must be fully closed. This phase modifies Phase 10 files (`calibration.h/.c`, `data_processing.c`, `debug_ui.c`, `main.c`) and FatFS middleware config. All new/modified code is CubeMX-safe (USER CODE sections or standalone application files).

**Detailed plan:** [phase_10b_cal_sd_partitions_751257fe.plan.md](/.cursor/plans/phase_10b_cal_sd_partitions_751257fe.plan.md)

**Actions:**

1. **Two-partition SD card layout** (64 GB target):
   - Partition 1 (`0:`, label `LOGGER`, ~32 GB): user-visible FAT32 for CSV export files and `README.txt`
   - Partition 2 (`1:`, label `SYSCAL`, ~32 GB): FAT32 for `.cal` files and binary logs — **after first-boot format**, MBR partition-type byte for partition 2 is patched to **0x83** (Linux) so **Windows Explorer** typically assigns a drive letter only to **LOGGER**; **SYSCAL** stays off “This PC” (Disk Management still shows the second partition, often labeled **Linux**). FatFS mounts **`1:`** via **`VolToPart[]`** (partition index), independent of the type byte.
   - `ffconf.h`: set `FF_MULTI_PARTITION = 1`, `FF_VOLUMES = 2`, `FF_USE_FIND = 1`, `FF_USE_LABEL = 1`
   - `fatfs.c`: add `VolToPart[]` mapping (`"0:" → phys 0, part 1`; `"1:" → phys 0, part 2`), second `FATFS` object, `sdMountAll()`
   - First boot with blank SD: firmware auto-formats via `f_fdisk()` + `f_mkfs()` ×2 + **MBR patch** (`disk_read`/`disk_write` LBA 0, `fmtWork[0x1D2] = 0x83`) + `sdMountAll()` + volume labels. UART: `[SD] format complete: LOGGER + SYSCAL`, `[SD] SYSCAL mounted`. Subsequent boots: `[SD] SYSCAL mounted` when dual mount succeeds.

2. **Per-cell binary calibration file** (`.cal`, 64 bytes packed):
   - Location: `1:<serialNumber>.cal` (e.g. `1:10326.cal`)
   - Struct `calFile_t`: magic `0x43414C31` (`'CAL1'`), serial number, version, 11 `calConfig_t` fields serialised as `float` in declaration order (including new `cellCorrFactor`), 8 reserved bytes, CRC16-CCITT of bytes 0..61
   - Factory values for four cells (SN 10326/10426/10526/10626) with shared `sensitivityUvPerN = 0.220919` and per-cell `cellCorrFactor` (0.973–1.019 range)
   - `tareOffsetN` always 0.0 in file (runtime-only via tare button)

3. **New `cellCorrFactor` field** added to `calConfig_t` (default 1.0f). **Hardware constant** `CH1_DIV_RATIO` added to `data_processing.h`. Force formula derived from ratiometric bridge measurement:
   ```
   #define CH1_DIV_RATIO  (33.0f / 133.0f)   /* R6/(R3+R6) from AFE schematic */

   /* Derivation:
    *   V_bridge       = F × sensitivity_eff              (bridge output ∝ force)
    *   V_ch1_actual   = V_exc × CH1_DIV_RATIO            (divider on excitation sense)
    *   acc_ratio      = accCh0 / accCh1 ≈ V_bridge / V_ch1_actual
    *   V_bridge       = acc_ratio × V_exc × CH1_DIV_RATIO
    *   F              = V_bridge / sensitivity_eff
    *                  = acc_ratio × V_exc × CH1_DIV_RATIO × 1e6 / (sensitivity_µV_per_N × cellCorrFactor)
    *
    *   For SN 10326:  3.3 × (33/133) × 1e6 / (0.220919 × 0.973379) ≈ 3,808,000
    */
   forceN = (accCh0_128 / accCh1_128)
          * (3.3f * CH1_DIV_RATIO * 1e6f / (sensitivityUvPerN * cellCorrFactor))
          - tareOffsetN;
   ```
   Full derivation with equation-by-equation traceability: see [Phase 10b detailed plan, Section 5](/.cursor/plans/phase_10b_cal_sd_partitions_751257fe.plan.md).
   `CH1_DIV_RATIO` accounts for the 100 kΩ (R3) / 33 kΩ (R6) voltage divider on the CH1 excitation sense path. AIN1_P sees `3.3 V × 0.2481 = 0.819 V`, not the full 3.3 V. Without this term, force reads ~4× too low. It is a fixed hardware constant — not stored in `.cal` files or `calConfig_t`.

4. **Rewrite `calibration.c`**: delete text-based `config.txt` parser entirely. Replace with binary `.cal` reader:
   - `calibrationLoadFromCal(uint32_t serialNumber)` — open, read 64 B, validate magic/version/CRC16, deserialise into `calConfig_t`, print each value
   - On failure (missing file, CRC mismatch): halt acquisition, set `STATE_ERROR`, trigger LED fault pattern. No silent fallback to defaults.
   - Track active serial number: `calibrationGetSerial()`

5. **Boot-time VT220 cell selection menu**:
   - After mounting both partitions, scan `1:*.cal` for up to 8 cells
   - Display numbered list on VT220 terminal; accept digit key `'1'`–`'8'` to select
   - If exactly 1 `.cal` file: auto-select (no menu). If 0 files: fault state.
   - Blocks until selection made — ADC streaming never starts without valid calibration
   - `uiSetCalSource("SN:XXXXX")` shows active cell on panel row 21

6. **Binary log files on partition 2**: `1:LOG_<bootSeconds>.bin` with pre-allocation from `calConfig_t.preallocMb`. CSV export to `0:` deferred to later phase (interface contract defined).

7. **Python factory tool** `Tools/write_cal.py`: CLI accepts serial + calibration values, writes 64-byte `.cal` with correct CRC16-CCITT. Companion `Tools/gen_all_cals.sh` generates all four cell files.

8. **ADS131M02 CH0 Gain Selection DOE** (factory, one-time per cell type):
   - CH1 (excitation sense) is **fixed at gain = 1**. AIN1_P sees 3.3 V × (33/133) = 0.819 V — already 68 % of the ±1.2 V full-scale at gain = 1. Any higher gain clips CH1.
   - CH0 (bridge sense) gain is determined by this DOE. ADS131M02 supports gains: 1, 2, 4, 8, 16, 32, 128 (no 64).
   - At 2000 kg FS, bridge output ≈ 4.33 mV differential. At gain = 128: 554 mV (46 % FS). At gain = 32: 139 mV (11.5 % FS).
   - **Test matrix:** loads at 0, 250, 500, 1000, 1500, 2000 kg (loading + unloading) × CH0 gains 1, 2, 4, 8, 16, 32, 128. Record 500 consecutive force records (1 s at 500 Hz) per point.
   - **Metrics per gain/load:** mean force N, RMS noise (σ of 500 samples), peak-to-peak noise, SNR, linearity R² across full range, max residual from linear fit.
   - **Pass criteria:** no clipping at any load point, RMS noise < 0.1 % FS (< 2 kg), R² > 0.9999, max residual < 5 kg.
   - The lowest gain meeting all criteria becomes the production gain. `adcGainCh1` in `calConfig_t` / `.cal` stores the selected CH0 gain; `adcGainCh2` stores CH1 gain (= 1, fixed). **`ads131m02Init()`** leaves PGA at **1×/1×**; **`ads131m02SetGain()`** applies factory gains **after** successful `calibrationLoadFromCal()`, before `dpInit()` / `ads131m02StartContinuous()`. Gain is not adjustable at runtime after boot.

**Key files:**
- `Middlewares/Third_Party/FatFs/src/ffconf.h` — edit: multi-partition, volumes, find
- `FATFS/App/fatfs.c` — edit: `VolToPart[]`, second `FATFS` object, dual mount
- `Core/Inc/calibration.h` — edit: add `cellCorrFactor` to `calConfig_t`, add `calFile_t`, new API
- `Core/Src/calibration.c` — rewrite: delete text parser, binary `.cal` reader + scan
- `Core/Inc/data_processing.h` — edit: add `CH1_DIV_RATIO` hardware constant
- `Core/Src/data_processing.c` — edit: force formula with `CH1_DIV_RATIO` and `cellCorrFactor`
- `Core/Src/debug_ui.c` — edit: cell selection menu + input handling
- `Core/Src/main.c` — edit: dual mount, first-boot format, **MBR patch** (`diskio.h`), README, cell selection, fault gate, UART `SYSCAL mounted`
- `Tools/write_cal.py` — new: Python `.cal` writer
- `Tools/gen_all_cals.sh` — new: batch generator for all 4 cells

**Success Criteria:**
- [ ] Fresh (blank) SD card triggers auto-format: two FAT32 partitions created, **MBR partition 2 type = 0x83**, volume labels `LOGGER` and `SYSCAL` set, UART prints `[SD] format complete: LOGGER + SYSCAL` and `[SD] SYSCAL mounted`
- [ ] **Windows:** after format, only **LOGGER** appears in Explorer with a drive letter; **SYSCAL** does not; Disk Management shows second partition as **Linux** — expected
- [ ] Pre-formatted card with existing partitions mounts directly (no re-format); UART prints `[SD] SYSCAL mounted` on successful dual mount
- [ ] `README.txt` created on partition 1 on first boot; not overwritten on subsequent boots
- [ ] `write_cal.py` produces a 64-byte `.cal` file whose CRC16 matches firmware `crc16Ccitt()`
- [ ] Valid `.cal` file loads: serial terminal prints all 11 field values, `calibrationGetSource() == CAL_SRC_SD_FILE`
- [ ] `.cal` file with a single flipped bit is rejected: UART prints CRC error, `STATE_ERROR` set, ADC streaming does not start, fault LED pattern active
- [ ] Missing `.cal` file (empty SYSCAL partition) triggers fault state identically to CRC failure
- [ ] `calConfig_t.cellCorrFactor` correctly loaded from `.cal` file (verify via UART print for each of the 4 cells)
- [ ] Force formula produces correct N with SN 10326 values: multiplier = `3.3 * (33/133) * 1e6 / (0.220919 * 0.973379)` = ~3,808,000; verify with known test signal or ADC test mode
- [ ] VT220 cell selection menu displays all scanned cells; digit key selects correct one
- [ ] Single `.cal` file on card: auto-selected without menu, UART prints `CAL: auto-select SN XXXXX`
- [ ] Panel row 21 shows `Cal: SN:XXXXX` after selection
- [ ] Cell selection is not persisted: power cycle requires re-selection
- [ ] Binary log file created on partition 2 (`1:LOG_*.bin`) with correct pre-allocation
- [ ] Both `"0:"` and `"1:"` drive paths work independently for read/write operations
- [ ] All new code follows Doxygen commenting standard (`@file`, `@brief`, `@param`, `@return`, `@note`, `@pre`, `@post`)
- [ ] All naming follows project conventions: `camelCase` functions, `camelCase_t` structs, `UPPER_SNAKE_CASE` defines, `g_` prefix globals
- [ ] `CH1_DIV_RATIO` defined in `data_processing.h` as `(33.0f / 133.0f)` — not in `calConfig_t` or `.cal` files
- [ ] CH1 gain fixed at 1 in firmware; `adcGainCh2 = 1` in all `.cal` files. No code path allows CH1 gain > 1
- [ ] Gain DOE: firmware applies CH0 gain via **`ads131m02SetGain()`** from `calConfig_t.adcGainCh1` / `adcGainCh2` after successful cal load — **not** inside `ads131m02Init()`; verify via readback or UART print
- [ ] Gain DOE pass criteria documented: no clipping, RMS noise < 0.1 % FS, R² > 0.9999, max residual < 5 kg
- [ ] All edits in CubeMX-generated files use USER CODE sections; all new files are standalone (CubeMX-safe)
- [ ] Zero DRDY misses over 60 s with calibration loaded (ISR budget unaffected)

---

### Phase 11 — Dual-File Logging Pipeline
**Goal:** Binary and CSV files written simultaneously to SD card with zero sample loss; 256 KB ring buffer absorbs FAT write stalls.

**Actions:**
1. Implement **256 KB ring buffer** (`circular_buffer.c/.h`):
   - Power-of-2 size for efficient wrap-around masking (no modulo)
   - Lock-free single-producer (ISR) / single-consumer (main loop) design
   - ISR pushes interleaved `bin_adc_record_t`, `bin_force_record_t`, and `bin_meta_record_t`
   - Main loop drains in 4 KB chunks via `f_write()`
   - Overflow counter incremented (not blocked) if buffer full

2. Implement **CSV line buffer** (~1 KB):
   - Separate from the binary ring buffer
   - ISR formats `$,time_ms,load_N,#\r\n` at 500 Hz and copies into this buffer
   - Main loop drains via `f_write()` to the CSV file

3. Implement `sdmmc_fatfs.c`: dual-file session lifecycle:
   - `sd_session_open()` — opens both `LOG_YYMMDD_HHMMSS.bin` and `LOG_YYMMDD_HHMMSS.csv`
   - Pre-allocate both files:
     ```c
     f_lseek(&g_bin_file, (FSIZE_t)g_cal.prealloc_mb * 1024 * 1024);
     f_lseek(&g_bin_file, 0);
     f_lseek(&g_csv_file, (FSIZE_t)(g_cal.prealloc_mb / 4) * 1024 * 1024);
     f_lseek(&g_csv_file, 0);
     ```
   - Write 64-byte `bin_file_header_t` to binary file at session start
   - Write CSV header comment lines at session start
   - `sd_session_close()` — truncate both files to actual size, close cleanly

4. **Metadata injection** (1/s from main loop):
   - Every second, assemble `bin_meta_record_t` with:
     - `clkin_hz` from `diag_clkin_measure_hz()`
     - `mcu_temp_x10` from `batt_get_mcu_temp_x10()`
     - `battery_mv` from `battery_get_voltage() * 1000`
     - `drdy_total`, `miss_total`, `overflow_total` cumulative counters
     - `ads_status` last STATUS word
   - Push into the binary ring buffer (same stream as ADC and Force records)

5. `logStart` button (PC4 EXTI4) toggles LOGGING state

6. Main loop flush order: binary ring buffer (4 KB chunks) → CSV line buffer → `ux_system_tasks_run()` → `cdc_poll()`

**Key files:**
- `Core/Src/circular_buffer.c` / `Core/Inc/circular_buffer.h` — new: 256 KB lock-free ring buffer
- `Core/Src/sdmmc_fatfs.c` / `Core/Inc/sdmmc_fatfs.h` — new: dual-file session lifecycle
- `Core/Src/app_state.c` / `Core/Inc/app_state.h` — new: IDLE/LOGGING/ERROR state machine

**Success Criteria (all tested with USB connected, `allow_log_on_usb=1`):**
- [ ] `logStart` button press starts logging; green NeoPixel lights
- [ ] Two files created on SD: `LOG_YYMMDD_HHMMSS.bin` and `LOG_YYMMDD_HHMMSS.csv`
- [ ] Both files open on PC (no FAT corruption)
- [ ] Binary file starts with valid 64-byte header (magic = `LDCL`, CRC OK)
- [ ] Binary ADC record count = elapsed_seconds x 8000 +/- 10
- [ ] Binary Force record count = elapsed_seconds x 500 +/- 5
- [ ] Binary Metadata record count = elapsed_seconds +/- 1
- [ ] CSV line count = elapsed_seconds x 500 +/- 5
- [ ] Every CSV line matches format `$,<integer>,<float>,#`
- [ ] All binary records pass CRC16 check
- [ ] `overflow_count == 0` after 5-minute test
- [ ] Second `logStart` press stops logging; both files truncated and closed cleanly
- [ ] Serial terminal shows: `LOG: started LOG_260411_142300.bin + .csv, prealloc=64MB`
- [ ] Serial terminal shows: `LOG: stopped, ADC=2400000 FORCE=150000 META=300, 0 overflows`
- [ ] Logging works while USB CDC debug output is streaming simultaneously
- [ ] VT220 UI updates (force, IMU, system fields) continue during active logging
- [ ] SD throughput stays below 200 KB/s sustained (measured via write timing)

---

### Phase 12 — One-Hour Soak Test and Python Validator
**Goal:** Prove zero sample loss over a full session; deliver `decode_bin.py` post-processing tool.

**Actions:**
1. Write `Tools/decode_bin.py` — Python script that:
   - Reads the 64-byte binary header, prints session info (CLKIN, DAC, calibration, FW version)
   - Demuxes records by type byte (0x01/0x02/0x03)
   - Validates CRC16 on every record, reports corruption count
   - Outputs separate CSVs: `_adc_8k.csv`, `_force_500.csv`, `_meta.csv`
   - Reconstructs exact timestamps using per-second CLKIN values from metadata records
   - Prints summary: total records, duration, average rates, any anomalies

2. Start logging with USB connected (`allow_log_on_usb=1`), leave running for 60 minutes

3. Monitor VT220 UI live via USB CDC for overflow count, DRDY Hz, SD throughput, and SD status

4. Stop logging, eject card, run `decode_bin.py` on the binary file

5. Spot-check CSV file in Excel/matplotlib (open directly, plot force vs time)

**Success Criteria (USB connected throughout):**
- [ ] Session duration = 60 min +/- 1 s (from metadata record count)
- [ ] Binary ADC records = **28,800,000 +/- 1000** (8000/s x 3600s)
- [ ] Binary Force records = **1,800,000 +/- 500** (500/s x 3600s)
- [ ] Binary Metadata records = **3600 +/- 2** (1/s x 3600s)
- [ ] CSV line count = **1,800,000 +/- 500** (500/s x 3600s)
- [ ] **`overflow_count == 0`** for entire session (from last metadata record)
- [ ] **All binary records pass CRC16** validation via `decode_bin.py` (zero corruption)
- [ ] Binary file size approximately 518 MB (144 KB/s x 3600s)
- [ ] CSV file size approximately 40 MB (11 KB/s x 3600s)
- [ ] Both files open on PC without FAT corruption
- [ ] No SD write errors reported (no ERROR state entered)
- [ ] CLKIN values in metadata records are stable (within +/- 1000 Hz of boot-trimmed value)
- [ ] MCU temperature values in metadata records are realistic and vary smoothly
- [ ] Battery voltage and SOC fields update every ~60 s on VT220 UI
- [ ] VT220 UI remained responsive and updated throughout the entire session
- [ ] USB CDC debug output did not cause any DRDY misses
- [ ] `decode_bin.py` runs without errors, produces valid output CSVs
- [ ] Force vs time plot from CSV shows physically plausible data

---

### Phase 13 — Calibration and Flash Storage (Production Readiness)
**Goal:** Calibration constants persist across power cycles; no SD card needed in field.

**Actions:**
1. Load calibration from Flash if SD config.txt absent
2. Implement `calibration_save_flash()` — callable only from IDLE state
3. USB CDC command `SAVE_CAL` triggers flash write
4. Verify Flash survives 10-cycle erase/write stress test

**Success Criteria:**
- [ ] Remove SD card, power cycle — calibration loads from Flash, `cal_source: FLASH` in report
- [ ] `SAVE_CAL` command writes to Flash; confirmed by power-cycle test
- [ ] Flash-loaded constants produce same force_N as SD-loaded constants

---

### Phase 14 — Documentation and Code Comments
**Goal:** Codebase is fully documented with Doxygen-style comments and consistent naming; a newcomer can understand the system from docs alone.

**Commenting Standard: Doxygen (agreed)**

Enforced by `.cursor/rules/commenting-and-naming.mdc`. Summary:

- **File headers:** Every `.c` and `.h` file gets `@file`, `@brief`, `@details` (what it does, why it exists, who feeds it, who consumes it), `@author`, `@date`.
- **Function docs:** Every non-static public function gets `@brief`, `@details`, `@param[in/out]`, `@return`, `@note` (hardware gotchas), `@pre`/`@post` (preconditions/postconditions), `@see` (cross-refs).
- **Inline comments:** Only for non-obvious logic, hardware quirks, errata workarounds, and datasheet citations. No narration of obvious code.
- **Tag minimum:** `@brief`, `@param`, `@return`, `@note`, `@pre`/`@post`, `@see`.

**Naming Convention (agreed)**

| Category | Convention | Example |
|---|---|---|
| `#define` constants | `UPPER_SNAKE_CASE` | `ADC_FRAME_SIZE_BYTES` |
| `enum` values | `UPPER_SNAKE_CASE` | `ADC_STATE_IDLE` |
| `const` variables | `UPPER_SNAKE_CASE` | `MAX_CHANNEL_COUNT` |
| Functions | `camelCase` | `ads131m02Init()` |
| Local variables | `camelCase` | `frameCount` |
| Global variables | `g_` prefix + `camelCase` | `g_adcReady` |
| Struct/typedef names | `camelCase_t` | `adcConfig_t` |
| Struct members | `camelCase` | `.sampleRate` |
| Module prefix | `lowercase` | `ads131m02_`, `lsm6dsv_` |

HAL/CubeMX-generated identifiers (`hspi1`, `HAL_GPIO_ReadPin`, etc.) are NOT renamed.

**Actions:**
1. ~~Define commenting standard~~ ✅ Agreed — Doxygen + UPPER_CASE constants + camelCase everything else
2. Review and document all application source files per the Doxygen standard (file headers, function docs, inline comments)
3. Rename any non-conforming constants to `UPPER_SNAKE_CASE`, variables/functions to `camelCase`
4. Write top-level project documentation (README, architecture overview, build instructions)
5. Document hardware connections, pin assignments, and board-specific details
6. Document the binary file format and CSV format for external consumers
7. Review and finalize `decode_bin.py` with usage instructions and inline docs

**Key files to review (in priority order):**
- `adc_ads131m02.c/.h` — most complex, ISR hot path
- `data_processing.c/.h` — decimation logic
- `circular_buffer.c/.h` — lock-free ring buffer
- `osc_ltc6903.c/.h` — SPI mode switch, DAC auto-trim
- `imu_lsm6dsv.c/.h` — IMU driver
- `sdmmc_fatfs.c/.h` — dual-file logging
- `battery_monitor.c/.h` — ADC1, charger decode
- `calibration.c/.h` — binary `.cal` file reader, cell scan, CRC16 validation (Phase 10b replaces config.txt parser)
- `debug_ui.c/.h` — VT220 panel
- `debug_uart.c/.h` — printf retarget, CDC state machine
- `app_state.c/.h` — state machine
- `log_record.h` — packed structs
- `diag_timers.c/.h` — frequency measurement
- `neopixel.c/.h` — WS2812 driver

**Success Criteria:**
- [x] Commenting standard agreed and documented (`.cursor/rules/commenting-and-naming.mdc`)
- [ ] Every `.c`/`.h` file has a Doxygen `@file` header block
- [ ] Every public function has `@brief`, `@param`, `@return` at minimum
- [ ] All `#define` constants and `enum` values are `UPPER_SNAKE_CASE`
- [ ] All functions and variables are `camelCase` (except HAL/CubeMX identifiers)
- [ ] No narration comments remain (e.g., `/* increment counter */`)
- [ ] All hardware-specific comments cite datasheet section or RM0481 reference
- [ ] Project-level documentation complete (README, architecture, build, hardware)
- [ ] A developer unfamiliar with the project can build, flash, and understand the firmware from documentation alone

---

## Module List

| File | Responsibility |
|------|---------------|
| `Core/Src/main.c` | Init sequence, main loop, state dispatch (USER CODE sections only) |
| `Core/Src/app_state.c/.h` | State machine: IDLE/LOGGING/ERROR transitions |
| `Core/Src/circular_buffer.c/.h` | Lock-free 256 KB ring buffer (binary records) + 1 KB CSV line buffer |
| `Core/Src/adc_ads131m02.c/.h` | ADS131M02 init (blocking) + DMA hot path |
| `Core/Src/osc_ltc6903.c/.h` | LTC6903 SPI Mode 0 init, frequency word, DAC auto-trim |
| `Core/Src/imu_lsm6dsv.c/.h` | LSM6DSV init + blocking SPI2 burst read |
| `Core/Src/data_processing.c/.h` | Two-stage boxcar decimation (8-sample + 128-sample), force_N, record assembly |
| `Core/Src/sdmmc_fatfs.c/.h` | Dual-file FatFS session lifecycle (binary + CSV) |
| `Core/Src/calibration.c/.h` | Binary `.cal` file reader, CRC16 validation, cell scan (Phase 10b replaces config.txt parser) |
| `Core/Inc/log_record.h` | Packed record type structs (header, ADC, Force+IMU, Metadata), validity flags, CRC16-CCITT |
| `Core/Src/neopixel.c/.h` | TIM2 CH1 WS2812 low-level driver: DMA buffer encode, one-shot PWM transfer |
| `Core/Src/led_status.c/.h` | High-level LED status engine: system priority table (LED 0), subsystem rotation (LED 1), blink patterns |
| `Core/Src/battery_monitor.c/.h` | ADC1 voltage + MCU temperature polling, BQ24012 PG/STAT1/STAT2 decode, 1 s cache |
| `Core/Src/debug_ui.c/.h` | VT220 ANSI status panel, scrolling log, report |
| `Core/Src/debug_uart.c/.h` | `_write()` -> USART1 + USBX CDC, CR+LF |
| `Core/Src/diag_timers.c/.h` | TIM3/TIM8 frequency measurement, DWT helpers — always compiled, production diagnostic |
| `Tools/decode_bin.py` | Python binary file post-processor: demux records, CRC validate, output CSVs, reconstruct timestamps |
| `Tools/write_cal.py` | Factory CLI: generate 64-byte binary `.cal` files with CRC16-CCITT per cell (Phase 10b) |
| `Tools/gen_all_cals.sh` | Batch script: invoke `write_cal.py` for all known load cells (Phase 10b) |
| `FATFS/App/fatfs.c` | `MX_FATFS_Init()`, dual-partition mount (`0:` LOGGER, `1:` SYSCAL), `VolToPart[]` (Phase 10b) |
| `FATFS/Target/sd_diskio.c` | FatFS diskio <-> SDMMC HAL bridge (DMA-based) |
| `Middlewares/Third_Party/FatFs/` | Elm Chan FatFS |
| `USBX/App/app_usbx.c` | USBX init, utility functions (`_ux_utility_time_get`) |
| `USBX/App/app_usbx_device.c` | USB device stack init, DCD registration |
| `USBX/App/ux_device_cdc_acm.c` | CDC ACM callbacks (activate/deactivate) |
| `USBX/App/ux_device_descriptors.c` | VID=0x0483 PID=0x5740, string descriptors |

---

## Key Risks

1. ~~**DMA buffer cache coherency**~~ **RESOLVED**: STM32H562 Cortex-M33 has **no D-cache** (only ICACHE). Plain RAM buffers are fine. No MPU region needed.

2. **HAL SPI re-entry** (Phase 7): `HAL_SPI_TransmitReceive_DMA()` returns `HAL_BUSY` if called before previous DMA completes. At 64 kHz with ~7.7 µs DMA transfer (12 bytes at 12.5 MHz) on a 15.625 µs DRDY period, there's 7.9 µs margin. Monitor with a miss counter.

3. **SD pre-allocation and card compatibility**: `f_lseek()` pre-allocation requires card to have enough free space. Log a warning if pre-allocation fails.

4. **Flash calibration during logging**: `calibration_save_flash()` must assert `STATE_IDLE`. Flash sector erase = 40–100 ms stall.

5. **USB DFU bootloader address**: `0x0BF97000` must be verified against RM0481 for STM32H562 specifically. If wrong, the board will HardFault on the DFU jump.

6. **LTC6903 SPI Mode switch**: Must complete before EXTI2 enabled. If called after sampling starts, it corrupts a live DMA transaction. Sequence enforced by `main.c` init order.

7. **CLOCK register bit fields**: Verify against ADS131M02 (not M04/M08) datasheet. OSR=128 → bits[4:2]=000. CLK_SEL (external clock) bit position must be confirmed.

8. **USBX CDC transmit from ISR**: `ux_device_class_cdc_acm_write_run()` is non-blocking but must NOT be called from ISR context. Debug printf must buffer to a secondary ring buffer; main loop drains it via USBX. USART1 can be called from ISR (blocking HAL_UART_Transmit with short timeout).

---

## NVIC Priority Map (lower number = higher priority)

| Priority | ISR | Rationale |
|----------|-----|-----------|
| 0 | EXTI2 (ADC DRDY) | Highest — must never miss a DRDY edge |
| 1 | GPDMA1 CH0/CH1 (SPI1 TX/RX) | DMA complete must fire before next DRDY |
| 2 | SPI1 (error handler) | SPI error recovery |
| 5 | SDMMC1 | SD card DMA — can tolerate brief delay |
| 6 | USB_DRD_FS | USB interrupt — debug only, lowest data-path priority |
| 7 | USART1 | Debug UART — lowest priority |
| 15 | SysTick | HAL tick — default |

> EXTI4 (logStart button) can be priority 8+ — button debounce makes latency irrelevant.

## Critical Files

| File | Why |
|------|-----|
| `Core/Src/adc_ads131m02.c` | DRDY ISR + DMA callback = entire 64 kHz hot path |
| `Core/Src/data_processing.c` | Two-stage decimation in ISR context — timing-critical record assembly |
| `Core/Src/circular_buffer.c` | 256 KB ISR-to-main lock-free handoff — correctness is safety-critical |
| `Core/Inc/log_record.h` | Packed record structs shared between ISR, main loop, and Python post-processor |
| `Core/Src/sdmmc_fatfs.c` | Dual-file pre-alloc + chunk write — primary source of potential data loss |
| `USBX/App/app_usbx.c` | Time tick function — if broken, USB CDC hangs |
| `FATFS/Target/sd_diskio.c` | FatFS-to-HAL_SD DMA bridge |
| `Core/Src/osc_ltc6903.c` | SPI mode switch + DAC auto-trim — must precede all ADS communication |
| `Core/Src/debug_ui.c` | First module implemented; foundation for all debugging |
| `Core/Src/calibration.c` | Boot-critical: CRC16 validation of `.cal` file gates acquisition start (Phase 10b) |
| `FATFS/App/fatfs.c` | Dual-partition mount + first-boot format — failure here prevents any SD access (Phase 10b) |
| `Middlewares/Third_Party/FatFs/src/ffconf.h` | Multi-partition + volume config — must match `VolToPart[]` exactly (Phase 10b) |

---

## Pre-Code Blockers and Resolutions

These must be addressed before or during Phase 1. Each includes exact file, location, and complete code.

---

### BLOCKER 1: Linker Script Heap/Stack Too Small

**Problem:** `_Min_Heap_Size = 0x200` (512 bytes). `printf("%.3f", val)` with newlib-nano calls `malloc()` internally for float formatting. 512 bytes is not enough — `malloc` returns NULL → HardFault or silent corruption. Stack at 4 KB may overflow with FatFS + printf + USB call chain.

**File:** `STM32H562RGTX_FLASH.ld` (lines 41–42)

**Fix — change two lines:**
```ld
_Min_Heap_Size = 0x4000; /* 16 KB — printf %f + FatFS needs this */
_Min_Stack_Size = 0x2000; /* 8 KB — FatFS + printf + USB call depth */
```

**Why 16 KB heap:** newlib-nano `_dtoa_r()` allocates ~4 KB on first float printf. FatFS `f_mount()` allocates `FATFS` struct (~1 KB). USB descriptors use additional heap. 16 KB provides margin.

**Why 8 KB stack:** Worst-case call depth: `main()` → `sd_session_write_chunk()` → `f_write()` → `disk_write()` → `HAL_SD_WriteBlocks()` + local buffers ≈ 3–4 KB. Adding printf VT220 formatting on top reaches ~5–6 KB.

**CubeMX safety:** No more regens planned, so direct edit is safe. If a regen IS done, restore these two lines manually.

**Verification:** After building, check `.map` file for heap/stack placement. At runtime, fill stack with a canary pattern (0xDEADBEEF) at boot and periodically check high-water mark.

---

### BLOCKER 2: USBX CDC Instance Pointer Not Stored

**Problem:** To transmit via CDC, you need the `UX_SLAVE_CLASS_CDC_ACM *cdc_acm` pointer. This pointer is only available when the USB host enumerates the device and USBX calls `USBD_CDC_ACM_Activate()`. The generated callback currently discards it (`UX_PARAMETER_NOT_USED`). Any CDC write before enumeration will dereference NULL → HardFault.

**File:** `USBX/App/ux_device_cdc_acm.c` (lines 65–72, 80–87)

**Fix — store the instance pointer in USER CODE sections:**
```c
/* ux_device_cdc_acm.c */

/* USER CODE BEGIN PV */
static UX_SLAVE_CLASS_CDC_ACM *g_cdc_acm = UX_NULL;
/* USER CODE END PV */

/* USER CODE BEGIN PFP */
UX_SLAVE_CLASS_CDC_ACM *cdc_acm_get_instance(void);
/* USER CODE END PFP */

VOID USBD_CDC_ACM_Activate(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_Activate */
  g_cdc_acm = (UX_SLAVE_CLASS_CDC_ACM *)cdc_acm_instance;
  /* USER CODE END USBD_CDC_ACM_Activate */
  return;
}

VOID USBD_CDC_ACM_Deactivate(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_Deactivate */
  UX_PARAMETER_NOT_USED(cdc_acm_instance);
  g_cdc_acm = UX_NULL;  /* Host disconnected — invalidate pointer */
  /* USER CODE END USBD_CDC_ACM_Deactivate */
  return;
}

/* USER CODE BEGIN 1 */
UX_SLAVE_CLASS_CDC_ACM *cdc_acm_get_instance(void)
{
  return g_cdc_acm;
}
/* USER CODE END 1 */
```

**Usage pattern (in debug_uart.c):**
```c
UX_SLAVE_CLASS_CDC_ACM *cdc = cdc_acm_get_instance();
if (cdc != UX_NULL) {
    /* Safe to call write_run */
}
/* If NULL, host not connected — skip CDC, USART1 still works */
```

**CubeMX safety:** All changes are inside USER CODE sections — survives regeneration.

---

### BLOCKER 3: USBX Memory Pool Too Small

**Problem:** `USBX_APP_MEM_POOL_SIZE = 1024` and `USBX_MEMORY_STACK_SIZE = 1024` in `app_usbx.h` line 44–45. USBX CDC ACM needs memory for: system struct (~300 bytes), device stack (~200 bytes), CDC class instance (~400 bytes), endpoint transfer buffers (64 bytes × 2 = 128 bytes), string descriptors, and internal alignment padding. 1024 bytes is not enough — `ux_system_initialize()` may succeed but CDC class registration will silently fail (no memory for class struct), resulting in USB device that enumerates but shows no COM port.

**File:** `USBX/App/app_usbx.h` (lines 44–45)

**Fix — increase pool sizes:**

These defines are OUTSIDE USER CODE sections, so CubeMX will overwrite them on regen. Two options:

**Option A (if no more CubeMX regens):** Edit directly:
```c
#define USBX_APP_MEM_POOL_SIZE       4096
#define USBX_MEMORY_STACK_SIZE       4096
```

**Option B (CubeMX-safe override):** In `app_usbx.c` USER CODE section, override the buffer size before it's used. However, the buffer is declared at file scope using the define, so this won't work. Instead, add to `app_usbx.h` in the USER CODE section after the defines:

```c
/* Exported constants --------------------------------------------------------*/
#define USBX_APP_MEM_POOL_SIZE       1024   /* <-- CubeMX generates this */
#define USBX_MEMORY_STACK_SIZE       1024   /* <-- CubeMX generates this */
/* USER CODE BEGIN EC */
/* Override CubeMX pool sizes — 1024 is too small for CDC ACM */
#undef USBX_APP_MEM_POOL_SIZE
#define USBX_APP_MEM_POOL_SIZE       4096
#undef USBX_MEMORY_STACK_SIZE
#define USBX_MEMORY_STACK_SIZE       4096
/* USER CODE END EC */
```

**Why 4096:** CDC ACM needs ~2.5 KB. 4096 provides margin for alignment overhead and future debug commands.

**Verification:** Check `ux_system_initialize()` return value. Check `ux_device_stack_class_register()` return value. Both must return `UX_SUCCESS`. If either fails, pool is too small.

---

### BLOCKER 4: CDC Write State Machine (Non-Blocking API)

**Problem:** `ux_device_class_cdc_acm_write_run()` is a non-blocking state machine. It does NOT send data in one call. Call flow:
1. First call: copies data to internal transfer buffer, starts USB transfer → returns `UX_STATE_WAIT`
2. Subsequent calls: checks if USB hardware finished → returns `UX_STATE_WAIT` again
3. Final call: transfer complete → returns `UX_STATE_NEXT`
4. Error: returns `UX_STATE_ERROR` or `UX_STATE_EXIT`

If you call it once and walk away, the data never reaches the host. If you call it with new data before the previous transfer completes, it corrupts the state machine.

**Resolution — design pattern for `debug_uart.c`:**

```c
/* debug_uart.c */
#include "ux_device_cdc_acm.h"

#define CDC_TX_BUF_SIZE  2048

static uint8_t  cdc_tx_buf[CDC_TX_BUF_SIZE];
static volatile uint32_t cdc_tx_head = 0;  /* Written by printf context */
static volatile uint32_t cdc_tx_tail = 0;  /* Read by cdc_poll() */

/* State for the non-blocking write_run state machine */
static uint8_t  cdc_tx_chunk[64];  /* USB FS max packet = 64 bytes */
static ULONG    cdc_tx_actual;
static enum { CDC_IDLE, CDC_SENDING } cdc_state = CDC_IDLE;

/* Called from printf → _write(). Adds data to ring buffer. */
static void cdc_enqueue(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (cdc_tx_head + 1) % CDC_TX_BUF_SIZE;
        if (next == cdc_tx_tail)
            break;  /* Buffer full — drop oldest silently */
        cdc_tx_buf[cdc_tx_head] = data[i];
        cdc_tx_head = next;
    }
}

/*
 * Called from main while(1) loop — pumps the CDC write state machine.
 * Must be called frequently (every iteration of main loop).
 */
void cdc_poll(void)
{
    UX_SLAVE_CLASS_CDC_ACM *cdc = cdc_acm_get_instance();
    if (cdc == UX_NULL)
        return;  /* Host not connected */

    UINT status;

    switch (cdc_state) {
    case CDC_IDLE:
        /* Anything to send? */
        if (cdc_tx_head == cdc_tx_tail)
            return;  /* Nothing queued */

        /* Dequeue up to 64 bytes into chunk buffer */
        {
            uint32_t n = 0;
            while (n < sizeof(cdc_tx_chunk) && cdc_tx_tail != cdc_tx_head) {
                cdc_tx_chunk[n++] = cdc_tx_buf[cdc_tx_tail];
                cdc_tx_tail = (cdc_tx_tail + 1) % CDC_TX_BUF_SIZE;
            }
            /* Kick off write_run with new data — first call sets up transfer */
            status = ux_device_class_cdc_acm_write_run(cdc, cdc_tx_chunk, n, &cdc_tx_actual);
            if (status == UX_STATE_WAIT) {
                cdc_state = CDC_SENDING;
            }
            /* UX_STATE_NEXT means it completed instantly (unlikely but possible) */
            /* UX_STATE_ERROR/EXIT — stay IDLE, data was dropped */
        }
        break;

    case CDC_SENDING:
        /* Continue pumping the state machine until transfer completes */
        status = ux_device_class_cdc_acm_write_run(cdc, cdc_tx_chunk, 0, &cdc_tx_actual);
        if (status == UX_STATE_NEXT || status >= UX_STATE_ERROR) {
            cdc_state = CDC_IDLE;  /* Done or error — ready for next chunk */
        }
        /* UX_STATE_WAIT — still sending, will try again next poll */
        break;
    }
}

/*
 * Newlib _write() retarget — called by printf.
 * Sends to USART1 (blocking, immediate) and CDC (buffered, polled later).
 */
int _write(int file, char *ptr, int len)
{
    (void)file;

    /* USART1: always send (blocking, short timeout) */
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, 10);

    /* CDC: enqueue to ring buffer — cdc_poll() drains it */
    cdc_enqueue((const uint8_t *)ptr, len);

    return len;
}
```

**Main loop integration:**
```c
/* main.c — USER CODE BEGIN 3 */
    cdc_poll();             /* Pump CDC TX state machine */
    ux_system_tasks_run();  /* Pump USBX device stack */
/* USER CODE END 3 */
```

**Key insight:** `_write()` never blocks on CDC — it just copies to a ring buffer. The main loop's `cdc_poll()` does the actual USB transfer in small non-blocking steps. If the USB host is not connected, `cdc_acm_get_instance()` returns NULL and CDC is silently skipped. USART1 always works regardless.

**IMPORTANT: `_write()` called from ISR context:** If `printf` is ever called from an ISR (e.g., for error logging), `HAL_UART_Transmit()` with a 10 ms timeout will block the ISR. For ISR safety, either:
- Never call printf from ISR (preferred — use a flag + main-loop print), or
- Use `HAL_UART_Transmit(&huart1, ..., 1)` with 1 ms timeout from ISR context

---

### BLOCKER 5: NVIC Priorities All at 0 (Equal Priority)

**Problem:** CubeMX set every interrupt to priority 0. This means:
- USB_DRD_FS interrupt can delay EXTI2 (DRDY) if USB fires first
- SDMMC1 interrupt can delay SPI1 DMA complete callback
- Even USART1 and TIM interrupts compete with the ADC hot path
- At 64 kHz DRDY rate (15.625 µs period), even a few µs of latency jitter can cause a missed DRDY edge while SPI1 DMA is still processing the previous sample

**Where priorities are set (CubeMX-generated, NOT in USER CODE sections):**

| File | Line | Current |
|------|------|---------|
| `gpio.c:134` | `EXTI2_IRQn` | 0 ✓ (keep) |
| `gpio.c:137` | `EXTI4_IRQn` | 0 (too high) |
| `gpdma.c:39` | `GPDMA1_Channel0_IRQn` | 0 ✓ (keep) |
| `gpdma.c:41` | `GPDMA1_Channel1_IRQn` | 0 ✓ (keep) |
| `spi.c:166` | `SPI1_IRQn` | 0 (fine, error only) |
| `sdmmc.c:106` | `SDMMC1_IRQn` | 0 (too high) |
| `usb.c:87` | `USB_DRD_FS_IRQn` | 0 (too high!) |
| `usart.c:110` | `USART1_IRQn` | 0 (too high) |
| `adc.c:117` | `ADC1_IRQn` | 0 (too high — battery ADC) |
| `tim.c:189` | `TIM2_IRQn` | 0 (neopixel — too high) |
| `tim.c:221` | `TIM3_IRQn` | 0 (diagnostics — too high) |

**Fix — override in `main.c` USER CODE BEGIN 2 (runs after all MX_Init functions):**

```c
/* main.c — USER CODE BEGIN 2 */

/* ── NVIC Priority Override ──────────────────────────────────────────
 * CubeMX sets everything to priority 0. We override here because
 * these lines are in a USER CODE section (CubeMX-safe).
 * Lower number = higher priority.  STM32H5 has 4 priority bits (0–15).
 *
 * Rule: ADC DRDY path (EXTI2 + SPI1 DMA) gets priority 0.
 *       Everything else is lower priority so it can never delay
 *       the 64 kHz sampling hot path.
 * ────────────────────────────────────────────────────────────────── */

/* Priority 0: ADC hot path — EXTI2 + DMA already at 0 from CubeMX, confirmed */
/* HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);            — already 0 ✓ */
/* HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);  — already 0 ✓ */
/* HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 0, 0);  — already 0 ✓ */

/* Priority 1: SPI1 error handler — must respond quickly to clear error */
HAL_NVIC_SetPriority(SPI1_IRQn, 1, 0);

/* Priority 4: SPI2 (IMU) — only used in blocking mode, but ISR still exists */
HAL_NVIC_SetPriority(SPI2_IRQn, 4, 0);

/* Priority 5: SDMMC — SD card IDMA complete, can tolerate brief delay */
HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0);

/* Priority 6: USB — debug CDC, never in data path */
HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 6, 0);

/* Priority 7: USART1 — debug UART */
HAL_NVIC_SetPriority(USART1_IRQn, 7, 0);

/* Priority 8: logStart button — human-speed, debounced */
HAL_NVIC_SetPriority(EXTI4_IRQn, 8, 0);

/* Priority 10: Battery ADC — once per 60 s, don't care about latency */
HAL_NVIC_SetPriority(ADC1_IRQn, 10, 0);

/* Priority 10: NeoPixel timer — cosmetic only */
HAL_NVIC_SetPriority(TIM2_IRQn, 10, 0);

/* Priority 10: Diagnostic timers — dev only */
HAL_NVIC_SetPriority(TIM3_IRQn, 10, 0);

/* Priority 12: Non-critical peripherals */
HAL_NVIC_SetPriority(CORDIC_IRQn, 12, 0);
HAL_NVIC_SetPriority(FMAC_IRQn, 12, 0);

/* USER CODE END 2 */
```

**CubeMX safety:** All overrides are in USER CODE BEGIN 2 — survives regeneration. The MX_Init functions set priority 0, then our code immediately overrides them before the main loop starts.

**Verification:** At runtime, read back priorities with `NVIC_GetPriority()` and print them in the startup banner to confirm they took effect.

---

### GOTCHA 6: FatFS Must Be Manually Added

**Problem:** FatFS is not a CubeMX-managed middleware in this project. SDMMC1 HAL is configured (sdmmc.c exists, `HAL_SD_Init` works), but there's no FatFS layer to provide `f_open()` / `f_write()` / `f_close()`. You need to add it yourself.

**Resolution — step by step:**

**Step 1: Copy FatFS source files.**
Source location (inside STM32CubeIDE installation):
```
C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.0.200.202406191623\tools\
```
Or download from http://elm-chan.org/fsw/ff/00index_e.html (R0.15 or later).

Create these directories and files:
```
Middlewares/Third_Party/FatFs/src/
    ff.c              — FatFS core
    ff.h              — FatFS API header
    ffconf.h          — Configuration (copy template, then edit)
    diskio.h          — Disk I/O interface definitions
    ffunicode.c       — Unicode support (needed if FF_USE_LFN > 0)

FATFS/Target/
    sd_diskio.c       — Bridge: FatFS diskio → HAL_SD_*
    sd_diskio.h

FATFS/App/
    fatfs.c           — MX_FATFS_Init() + f_mount()
    fatfs.h
```

**Step 2: `ffconf.h` key settings:**
```c
#define FF_FS_TINY       0       /* Full buffering (not tiny) */
#define FF_USE_FASTSEEK  1       /* Pre-allocation support */
#define FF_USE_LFN       1       /* Long file names (heap alloc) */
#define FF_LFN_UNICODE   0       /* OEM char set */
#define FF_FS_REENTRANT  0       /* No RTOS, no reentrancy needed */
#define FF_FS_NORTC      0       /* Use RTC for timestamps */
#define FF_MIN_SS        512
#define FF_MAX_SS        512
#define FF_VOLUMES       1
#define FF_USE_MKFS      1       /* Allow formatting from firmware */
#define FF_CODE_PAGE     437     /* US English */
```

**Step 3: `sd_diskio.c` — the critical bridge:**

```c
/* sd_diskio.c — FatFS ↔ HAL_SD bridge */
#include "ff.h"
#include "diskio.h"
#include "sdmmc.h"

extern SD_HandleTypeDef hsd1;

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    /* SDMMC1 already initialized by MX_SDMMC1_SD_Init() */
    if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER)
        return 0;  /* RES_OK */
    return STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER)
        return 0;
    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0) return RES_PARERR;
    if (HAL_SD_ReadBlocks(&hsd1, buff, sector, count, 5000) != HAL_OK)
        return RES_ERROR;
    /* Wait for card to return to transfer state */
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {}
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0) return RES_PARERR;
    if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buff, sector, count, 5000) != HAL_OK)
        return RES_ERROR;
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {}
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) return RES_PARERR;
    HAL_SD_CardInfoTypeDef info;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        HAL_SD_GetCardInfo(&hsd1, &info);
        *(DWORD *)buff = info.LogBlockNbr;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        HAL_SD_GetCardInfo(&hsd1, &info);
        *(DWORD *)buff = info.LogBlockSize / 512;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

/* RTC timestamp for FatFS */
DWORD get_fattime(void)
{
    /* TODO: Read RTC and return packed FAT timestamp */
    /* For now, return a fixed date: 2026-01-01 00:00:00 */
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) |
           ((DWORD)1 << 16) | (0 << 11) | (0 << 5) | (0 >> 1);
}
```

**Step 4: Add include paths to build configuration.**
In STM32CubeIDE: Project Properties → C/C++ Build → Settings → MCU GCC Compiler → Include paths. Add:
```
../Middlewares/Third_Party/FatFs/src
../FATFS/App
../FATFS/Target
```

**Note on DMA vs polling for SD (RESOLVED in Phase 4):** Polling (`HAL_SD_ReadBlocks` / `HAL_SD_WriteBlocks`) fails with `DATA_TIMEOUT` on STM32H5 in 4-bit mode. **DMA is mandatory**, not optional. `sd_diskio.c` uses `HAL_SD_ReadBlocks_DMA` / `HAL_SD_WriteBlocks_DMA` with completion callbacks (`HAL_SD_RxCpltCallback`, `HAL_SD_TxCpltCallback`) and timeout-protected wait loops. A 4-byte aligned scratch buffer handles unaligned FatFS buffers. Measured throughput: ~325 KB/s at 25 MHz, 2 KB flush chunks give 46 ms worst-case write latency.

---

### GOTCHA 7: `_ux_utility_time_get()` Returns 0

**Problem:** USBX uses `_ux_utility_time_get()` for timeouts (control transfer timeout = 10000 ms, device attach timeout, etc.). The generated function returns `0` — every timeout check thinks zero time has elapsed, causing either infinite waits or immediate timeouts depending on the comparison direction.

**File:** `USBX/App/app_usbx.c` (lines 108–117)

**Fix — one line in USER CODE section:**
```c
ULONG _ux_utility_time_get(VOID)
{
  ULONG time_tick = 0U;

  /* USER CODE BEGIN _ux_utility_time_get */
  time_tick = (ULONG)HAL_GetTick();  /* SysTick-based, 1 ms resolution */
  /* USER CODE END _ux_utility_time_get */

  return time_tick;
}
```

**Why HAL_GetTick():** USBX `UX_PERIODIC_RATE` is set to 1000 in `ux_user.h` (1 tick = 1 ms). `HAL_GetTick()` also returns milliseconds from SysTick. The units match.

**CubeMX safety:** Inside USER CODE section — survives regeneration.

---

### GOTCHA 8: LTC6903 SPI Mode Switch Must Precede EXTI2 Enable

**Problem:** LTC6903 uses SPI Mode 0 (`CPOL=0, CPHA=0`). ADS131M02 uses SPI Mode 1 (`CPOL=0, CPHA=1`). Both share SPI1. The mode switch requires disabling SPI1, changing `CLKPhase`, re-initializing, and re-enabling. If EXTI2 is already enabled and a DRDY edge fires during the mode switch, the ISR will start a DMA transfer on a half-configured SPI peripheral → bus corruption, potential HardFault.

**Resolution — enforce init order in `main.c` USER CODE BEGIN 2:**

```c
/* main.c — USER CODE BEGIN 2 */

/* ── Init Order (CRITICAL) ──────────────────────────────────────────
 * 1. NVIC priority overrides (done above)
 * 2. DFU bootloader check
 * 3. LTC6903 oscillator init  ← SPI1 Mode 0, EXTI2 NOT yet enabled
 * 4. ADS131M02 register init  ← SPI1 Mode 1 (restored by LTC6903 init)
 * 5. LSM6DSV IMU init         ← SPI2, independent
 * 6. FatFS mount + calibration load
 * 7. USB DCD registration
 * 8. Enable EXTI2             ← NOW safe, all SPI1 init is done
 * 9. Start sampling
 * ────────────────────────────────────────────────────────────────── */

/* IMPORTANT: EXTI2 is enabled by MX_GPIO_Init() (line 100 of main.c).
 * CubeMX calls HAL_NVIC_EnableIRQ(EXTI2_IRQn) inside gpio.c.
 * We must DISABLE it here and re-enable after LTC6903+ADS init. */
HAL_NVIC_DisableIRQ(EXTI2_IRQn);  /* Suppress DRDY during init */

/* 2. DFU bootloader check */
if (HAL_GPIO_ReadPin(userButton_GPIO_Port, userButton_Pin) == GPIO_PIN_RESET) {
    /* ... DFU jump code ... */
}

/* 3. LTC6903 — sets SPI1 to Mode 0, writes frequency word, restores Mode 1 */
ltc6903_init();

/* 4. ADS131M02 — register config (blocking SPI1, Mode 1) */
ads131m02_init();

/* 5. LSM6DSV — register config (blocking SPI2) */
lsm6dsv_init();

/* 6. FatFS + calibration */
MX_FATFS_Init();
calibration_load();

/* 7. USB DCD */
ux_dcd_stm32_initialize((ULONG)USB_DRD_FS, (ULONG)&hpcd_USB_DRD_FS);
HAL_PCD_Start(&hpcd_USB_DRD_FS);

/* 8. NOW enable EXTI2 — all SPI1 init is complete */
__HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_Pin);  /* Clear any pending edge from init */
HAL_NVIC_EnableIRQ(EXTI2_IRQn);

/* USER CODE END 2 */
```

**CubeMX safety:** USER CODE BEGIN 2 survives regeneration. The `HAL_NVIC_DisableIRQ` at the top counteracts the `HAL_NVIC_EnableIRQ` in `gpio.c`.

---

### GOTCHA 9: USB DCD Registration Location and Startup Sequence

**Problem:** USBX device stack is initialized by `MX_USBX_Init()` (line 116 of main.c), which calls `ux_system_initialize()` and registers the CDC ACM class. But the DCD (Device Controller Driver) that connects USBX to the STM32 USB hardware is NOT registered by CubeMX — that call is missing. Without it, USBX has no hardware backend → device never enumerates.

Additionally, `HAL_PCD_Start()` must be called to physically enable the USB pull-up resistor so the host detects the device.

**File:** `Core/Src/main.c` USER CODE BEGIN 2

**Fix — add after MX_USBX_Init() but before main loop:**
```c
/* Register STM32 USB hardware controller with USBX */
if (ux_dcd_stm32_initialize((ULONG)USB_DRD_FS, (ULONG)&hpcd_USB_DRD_FS) != UX_SUCCESS) {
    /* DCD init failed — USB won't work but don't halt (USART1 still available) */
    printf("ERROR: USB DCD init failed\r\n");
}

/* Enable USB pull-up — host will detect the device now */
HAL_PCD_Start(&hpcd_USB_DRD_FS);
```

**Required headers (add to main.c USER CODE BEGIN Includes):**
```c
/* USER CODE BEGIN Includes */
#include "ux_dcd_stm32.h"    /* For ux_dcd_stm32_initialize() */
#include "debug_uart.h"       /* For printf retarget */
/* USER CODE END Includes */
```

**Main loop — add USBX polling (USER CODE BEGIN 3):**
```c
/* USER CODE BEGIN 3 */
    ux_system_tasks_run();   /* Pump USBX standalone state machine */
    cdc_poll();              /* Drain CDC TX ring buffer */
/* USER CODE END 3 */
```

**Startup timing note:** After `HAL_PCD_Start()`, the USB host takes 100–500 ms to enumerate the device. During this time, `cdc_acm_get_instance()` returns NULL. USART1 printf works immediately; CDC printf starts working only after enumeration completes and `USBD_CDC_ACM_Activate()` fires.

**CubeMX safety:** All changes in USER CODE sections.
