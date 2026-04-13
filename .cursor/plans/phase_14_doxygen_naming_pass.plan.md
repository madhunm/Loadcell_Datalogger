---
name: ""
overview: ""
todos: []
isProject: false
---

# Phase 14 — Doxygen Commenting & Naming Convention Pass

## Standard Reference

Enforced by `.cursor/rules/commenting-and-naming.mdc`. Applied at Phase 14 after all functional code is complete and tested.

### Commenting: Doxygen
- File headers: `@file`, `@brief`, `@details`, `@author`, `@date`
- Functions: `@brief`, `@details`, `@param[in/out]`, `@return`, `@note`, `@pre`/`@post`, `@see`
- Inline: only non-obvious logic, hardware quirks, datasheet/RM0481 citations
- Forbidden: narration comments (`/* increment counter */`)

### Naming
- Constants / `#define` / `enum` values: `UPPER_SNAKE_CASE`
- Functions / variables / struct members: `camelCase`
- Globals: `g_` prefix + `camelCase`
- Struct/typedef: `camelCase_t`
- HAL/CubeMX identifiers: do NOT rename

---

## Scope: Application Files Only

CubeMX-generated files and third-party middleware are excluded. Only document USER CODE sections within CubeMX files.

---

## Execution Plan

### Step 1 — ADC Hot Path (highest risk, most complex)

| File | Actions |
|------|---------|
| `Core/Inc/adc_ads131m02.h` | Add `@file` header. Doxygen all public function prototypes. Verify all `#define` are `UPPER_SNAKE_CASE`. |
| `Core/Src/adc_ads131m02.c` | Add `@file` header. Doxygen every function (public and important static). Add `@note` for GPDMA register-level quirks, `@see` RM0481 sections. Rename any non-conforming variables/functions to `camelCase`. |
| `Core/Src/stm32h5xx_it.c` | Document USER CODE ISR handlers with `@brief` explaining the DRDY → DMA → complete chain. |

**Verify:** Build compiles. Run 60 s DRDY test — zero misses.

### Step 2 — Oscillator & Diagnostics

| File | Actions |
|------|---------|
| `Core/Inc/osc_ltc6903.h` | `@file` header, Doxygen prototypes. |
| `Core/Src/osc_ltc6903.c` | `@file` header, Doxygen all functions. `@note` SPI mode switch. `@see` LTC6903 datasheet. |
| `Core/Inc/diag_timers.h` | `@file` header, Doxygen prototypes. |
| `Core/Src/diag_timers.c` | `@file` header, Doxygen all functions. `@note` DWT CYCCNT usage. |

**Verify:** Build compiles. CLKIN measurement and auto-trim still work.

### Step 3 — IMU Driver

| File | Actions |
|------|---------|
| `Core/Inc/imu_lsm6dsv.h` | `@file` header, Doxygen prototypes. |
| `Core/Src/imu_lsm6dsv.c` | `@file` header, Doxygen all functions. `@see` LSM6DSV datasheet register refs. |

**Verify:** Build compiles. WHO_AM_I = 0x70, accel Z shows gravity.

### Step 4 — Debug Output

| File | Actions |
|------|---------|
| `Core/Inc/debug_uart.h` | `@file` header, Doxygen prototypes. |
| `Core/Src/debug_uart.c` | `@file` header, Doxygen all functions. `@details` CDC state machine flow. |
| `Core/Inc/debug_ui.h` | `@file` header, Doxygen prototypes. |
| `Core/Src/debug_ui.c` | `@file` header, Doxygen all functions. `@details` VT220 escape sequences. |

**Verify:** Build compiles. VT220 panel renders correctly. USB CDC still enumerates.

### Step 5 — SD / FatFS / Logging (when these files exist, Phase 11+)

| File | Actions |
|------|---------|
| `Core/Inc/log_record.h` | `@file` header. Doxygen every struct field (inline `/**< */` or block). Document record types, CRC polynomial. |
| `Core/Src/circular_buffer.c/.h` | `@file` header. Doxygen all functions. `@note` lock-free single-producer/single-consumer contract. |
| `Core/Src/sdmmc_fatfs.c/.h` | `@file` header. Doxygen all functions. `@note` pre-allocation, DMA-mandatory. |
| `Core/Src/calibration.c/.h` | `@file` header. Doxygen all functions. Document config.txt keys. |
| `Core/Src/app_state.c/.h` | `@file` header. Doxygen state machine transitions. |

**Verify:** Build compiles. 5-minute logging test — zero overflows, files valid.

### Step 6 — Remaining Modules

| File | Actions |
|------|---------|
| `Core/Src/battery_monitor.c/.h` | `@file` header. Doxygen all functions. SOC table comments. |
| `Core/Src/neopixel.c/.h` | `@file` header. Doxygen all functions. |
| `Core/Src/custom_bus.c` | `@file` header on USER CODE only (CubeMX-generated). |

**Verify:** Build compiles.

### Step 7 — Naming Audit

Scan all application files for naming violations:
1. Any `#define` or `enum` value not in `UPPER_SNAKE_CASE` → rename
2. Any function or variable not in `camelCase` → rename
3. Any global missing `g_` prefix → add prefix
4. Update all call sites after any rename
5. **Do NOT touch** HAL/CubeMX identifiers

**Verify:** Build compiles. Full regression: DRDY test + SD logging + USB CDC.

### Step 8 — Project Documentation

1. `README.md` — project overview, build instructions, hardware requirements, quick start
2. `Docs/ARCHITECTURE.md` — system block diagram, data flow, ISR chain, timing budget
3. `Docs/HARDWARE.md` — pin assignments, SPI bus sharing, clock tree, BQ24012 wiring
4. `Docs/BINARY_FORMAT.md` — complete binary file spec for `decode_bin.py` consumers
5. `Tools/decode_bin.py` — docstrings, usage examples, inline comments

---

## When to Execute This Plan

**Trigger:** All functional phases (1–13) are complete and tested. No more structural code changes expected.

**Why not earlier:** Commenting and renaming during active development creates merge friction and wastes effort on code that may be rewritten. The `.cursor/rules/commenting-and-naming.mdc` rule ensures any *new* code written from now on follows the standard automatically. The full audit pass happens once at Phase 14.

**Exception — apply incrementally during development:**
- New files created in Phase 8+ should follow the standard from creation (the rule file handles this).
- If a file is substantially rewritten, apply the standard at that time.
- The Phase 14 pass catches anything that was missed.

---

## Success Criteria

- [x] Every application `.c`/`.h` has a Doxygen `@file` header
- [x] Every public function has `@brief`, `@param`, `@return` minimum
- [x] All constants/enums are `UPPER_SNAKE_CASE`
- [x] All functions/variables are `camelCase`
- [x] All globals have `g_` prefix
- [x] No narration comments remain
- [x] Hardware comments cite datasheet or RM0481
- [ ] README, ARCHITECTURE, HARDWARE, BINARY_FORMAT docs exist  *(Phase 14, Step 8 — deferred)*
- [ ] Build compiles with zero warnings  *(requires on-target build)*
- [ ] Full regression passes (60 s DRDY + SD logging + USB CDC)  *(requires on-target test)*

---

## Phase 14a — COMPLETED (2026-04-12)

### Pass 1: Phase 8 files (imu_lsm6dsv.h/.c)
- Already fully compliant from creation. No changes needed.

### Pass 2: Phases 0-7 files — Doxygen documentation
All application files received comprehensive Doxygen `@file` headers, function documentation
with `@brief`, `@param`, `@return`, `@note`, `@pre`, `@post`, `@see` as appropriate.
CubeMX-generated files (main.c, stm32h5xx_it.c) were documented in USER CODE sections only.

### Pass 3: Phases 0-7 files — Naming convention enforcement
All snake_case identifiers in application code renamed to camelCase:
- **Public functions:** ~60 functions across 5 modules (ads131m02, ltc6903, diag, uart, ui)
- **Typedef:** `ads_dma_stats_t` → `adsDmaStats_t`, `cdc_state_t` → `cdcState_t`
- **Struct members:** 16 members in `adsDmaStats_t`
- **Static functions:** ~25 across all modules
- **Static variables:** ~50 across all modules
- **Local variables/parameters:** ~80 across all modules
- **Globals:** `g_ltc_word` → `g_ltcWord`, `g_measured_hz` → `g_measuredHz`
- **All call sites** updated in main.c, stm32h5xx_it.c, and cross-module references

### Verification
Grep scan confirms zero residual snake_case application identifiers remain.
HAL/CubeMX identifiers and `#define`/`enum` constants were not touched.