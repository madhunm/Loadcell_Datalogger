---
name: Phase 5 LTC6903 Oscillator
overview: Implement the LTC6903 programmable oscillator driver to produce 8.192 MHz CLKIN for the ADS131M02, plus TIM8-based frequency measurement to verify output accuracy. All code is CubeMX-safe.
todos:
  - id: create-osc-ltc6903
    content: Create osc_ltc6903.c/h -- LTC6903 SPI Mode 0 init, frequency word 0xCF60, mode switch back to Mode 1
    status: completed
  - id: create-diag-timers
    content: Create diag_timers.c/h -- TIM8 external clock counting, overflow ISR, 1-second frequency computation, uiSetClkinHz integration
    status: completed
  - id: restructure-main-init
    content: "Restructure main.c USER CODE BEGIN 2 -- uncomment GPDMA/SPI1/TIM8, add NVIC overrides, CS deassert, ltc6903Init, diagClkinInit, wrap SD benchmarks in #if 0"
    status: completed
  - id: update-main-loop
    content: Add diagClkinPoll() to main loop and includes to main.c
    status: completed
  - id: verify-build
    content: Verify build compiles without errors or warnings, check lint
    status: completed
isProject: false
---

# Phase 5 -- LTC6903 Oscillator + CLKIN Verification

## Goal

Program the LTC6903 to output 8.192 MHz (the ADS131M02 CLKIN), then verify the output frequency using TIM8 external clock counting on PC6/PC7. Display on VT220 UI and USART1.

## Hardware Context

- **LTC6903** is on the SPI1 bus (PA5 SCK, PA6 MISO, PA7 MOSI) with its own CS on **PC5** (`LTC_CS_Pin`).
- LTC6903 uses **SPI Mode 0** (CPOL=0, CPHA=0). ADS131M02 uses **SPI Mode 1** (CPOL=0, CPHA=1). Both share SPI1 -- mode switch required.
- SPI1 clock = PLL1Q / 4 = 50 MHz / 4 = **12.5 MHz**.
- CLKIN output from LTC6903 is routed to **PC6 only** (TIM8_CH1, labeled `CLKIN_Reader`). **PC7 (TIM8_CH2) has no signal.**
- TIM8 CubeMX config: External Clock Mode 1 via TI2FP2 (PC7) -- **this won't work as-is** because PC7 is unconnected. Runtime override in `diagClkinInit()` switches the trigger to **TI1FP1** (PC6) so the counter clocks from the actual CLKIN signal. 16-bit counter, no prescaler.

## MikroE Reference Driver ([References/MikroE/](References/MikroE/))

The MikroE ADC 15 Click driver (`adc15.c`) targets the same LTC6903 + ADS131M02 combo. Key confirmations and notes:

- `**adc15_ltc_write()`** (adc15.c:355-372): Switches SPI to Mode 0, builds `(oct << 12) | (dac << 2) | (cnf)`, sends 2 bytes MSB-first via separate CS pin (`cs2`), then restores Mode 1. Exact pattern we will follow.
- `**adc15_set_frequency()**` (adc15.c:374-379): Uses `oct = 3.322 * log10(freq / 1039.0)` and `dac = 2048.0 - (2078 * 2^(10+oct) / freq)`. Formulas match our calculation below.
- `**adc15_default_cfg()**` (adc15.c:170-188): Sets LTC6903 frequency FIRST, then resets ADS131M02, then configures ADS registers. Confirms our Phase 5 (LTC) -> Phase 6 (ADS) ordering.
- **CNF discrepancy**: MikroE uses `ADC15_LDC_CFG_POWER_ON = 2` (CNF = 0b10). Per the LTC6903 datasheet Table 1, CNF = 0b10 means **power down** (both outputs disabled). CNF = 0b00 means both CLK and CLK-bar enabled. **Our implementation uses CNF = 0b00** which is unambiguously "outputs enabled."

## Frequency Word Calculation

For f_target = 8,192,000 Hz (LTC6903 datasheet formulas, confirmed by MikroE `adc15_set_frequency`):

- **OCT** = floor(3.322 * log10(8192000 / 1039)) = floor(3.322 * 3.8969) = **12**
- **DAC** = round(2048 - (2078 * 2^22) / 8192000) = round(2048 - 1063.94) = **984**
- **CNF** = 0b00 (CLK active on both pins -- NOT 0b10 as in MikroE code)

Verification: f_out = 2078 * 2^22 / (2048 - 984) = 8,715,783,7LTC6903: init OK, word=0xCF60 (OCT=12 DAC=984 CNF=0)

LTC6903: target=8192000 Hz, computed=8191507.2 Hz

LTC6903: SPI mode switch Mode0->Mode1 complete

DIAG: TIM8 CLKIN counter started (TI1FP1/PC6)

LTC6903: init OK, word=0xCF60 (OCT=12 DAC=984 CNF=0)

LTC6903: target=8192000 Hz, computed=8191507.2 Hz

LTC6903: SPI mode switch Mode0->Mode1 complete

DIAG: TIM8 CLKIN counter started (TI1FP1/PC6)

12 / 1064 = **8,191,995.5 Hz** (error = 0.00055%, well within 0.5%)

**SPI word** = (12 << 12) | (984 << 2) | 0x00 = **0xCF60**. Transmitted as bytes: `{0xCF, 0x60}`.

## Frequency Measurement Strategy

TIM8 counter is clocked by CLKIN via **TI1FP1 (PC6)** -- overridden at runtime from the CubeMX default of TI2FP2 (PC7) because PC7 is unconnected. At 8.192 MHz the 16-bit counter overflows ~125 times/second. The `TIM8_UP_IRQHandler` increments a software overflow counter. Every 1 second, `diagClkinPoll()` reads (overflows * 65536 + CNT), computes delta, and derives Hz.

## Files to Create

### 1. [Core/Inc/osc_ltc6903.h](Core/Inc/osc_ltc6903.h) (new, CubeMX-safe)

```c
/**
 * @file    osc_ltc6903.h
 * @brief   LTC6903 programmable oscillator driver (8.192 MHz CLKIN).
 * @author  Madhu
 * @date    YYYY-MM-DD
 */
#ifndef OSC_LTC6903_H
#define OSC_LTC6903_H
#ifdef __cplusplus
extern "C" {
#endif
int ltc6903Init(void);
#ifdef __cplusplus
}
#endif
#endif
```

### 2. [Core/Src/osc_ltc6903.c](Core/Src/osc_ltc6903.c) (new, CubeMX-safe)

Key logic (follows MikroE `adc15_ltc_write` pattern, adapted for STM32 HAL):

- Deassert both ADC_CS and LTC_CS (HIGH) for clean state
- Temporarily switch SPI1 from Mode 1 to Mode 0 (matching MikroE `spi_master_set_mode(&ctx->spi, SPI_MASTER_MODE_0)`)
- Assert LTC_CS (LOW), send `{0xCF, 0x60}` via blocking `HAL_SPI_Transmit`, deassert LTC_CS (HIGH)
- Switch SPI1 back to Mode 1 (matching MikroE `spi_master_set_mode(&ctx->spi, SPI_MASTER_MODE_1)`)
- Print init status via printf

SPI mode switch pattern (from the master plan):

```c
__HAL_SPI_DISABLE(&hspi1);
hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;   // Mode 0
HAL_SPI_Init(&hspi1);
__HAL_SPI_ENABLE(&hspi1);

HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_RESET);  // Assert CS
HAL_SPI_Transmit(&hspi1, txData, 2, 100);                        // Send 0xCF 0x60
HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_SET);    // Deassert CS

__HAL_SPI_DISABLE(&hspi1);
hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;   // Restore Mode 1
HAL_SPI_Init(&hspi1);
__HAL_SPI_ENABLE(&hspi1);
```

### 3. [Core/Inc/diag_timers.h](Core/Inc/diag_timers.h) (new, CubeMX-safe)

```c
/**
 * @file    diag_timers.h
 * @brief   Hardware timer-based frequency measurement (CLKIN and DRDY).
 * @author  Madhu
 * @date    YYYY-MM-DD
 */
#ifndef DIAG_TIMERS_H
#define DIAG_TIMERS_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
void diagClkinInit(void);
void diagClkinPoll(void);   // call from main loop
uint32_t diagClkinGetHz(void);
#ifdef __cplusplus
}
#endif
#endif
```

### 4. [Core/Src/diag_timers.c](Core/Src/diag_timers.c) (new, CubeMX-safe)

Key logic:

- `diagClkinInit()`:
  - **Override TIM8 slave mode** to use **TI1FP1** (PC6) instead of TI2FP2 (PC7). This is mandatory -- CLKIN is only connected to PC6; PC7 is unconnected. Without this override, the counter receives no clock edges and stays at zero.
  - Enable TIM8 update interrupt: `HAL_NVIC_SetPriority(TIM8_UP_IRQn, 10, 0); HAL_NVIC_EnableIRQ(TIM8_UP_IRQn);`
  - Start timer: `__HAL_TIM_ENABLE(&htim8);` with update interrupt `__HAL_TIM_ENABLE_IT(&htim8, TIM_IT_UPDATE);`
- `TIM8_UP_IRQHandler()`: Clear flag, increment `volatile uint32_t g_tim8OvfCount`
- `diagClkinPoll()`:
  - Every 1000 ms (using `HAL_GetTick`), atomically read overflow count + CNT
  - Compute `total = ovf * 65536 + cnt`, delta from previous, derive Hz
  - Call `uiSetClkinHz(freq_hz)` and print to USART1

**Critical note:** The `TIM8_UP_IRQHandler` is defined in this new file, not in `stm32h5xx_it.c`. Since CubeMX didn't generate it (TIM8 NVIC was never enabled in IOC), there is no conflict. CubeMX-safe.

## Files to Modify

### 5. [Core/Src/main.c](Core/Src/main.c) -- USER CODE sections only

Changes in **USER CODE BEGIN Includes**:

- Add `#include "osc_ltc6903.h"` and `#include "diag_timers.h"`

Changes in **USER CODE BEGIN 2** -- restructure init sequence:

1. **Uncomment** `MX_GPDMA1_Init()` (required by SPI1 DMA config in MspInit)
2. **Uncomment** `MX_SPI1_Init()` (needed for LTC6903 and later ADS131M02)
3. **Uncomment** `MX_TIM8_Init()` (needed for CLKIN frequency measurement)
4. Add `HAL_NVIC_DisableIRQ(EXTI2_IRQn);` right after GPIO init section (per GOTCHA 8 -- suppress DRDY during init)
5. Add NVIC priority overrides (from BLOCKER 5 in master plan) -- SPI1 prio 1, SDMMC prio 5, USB prio 6, USART1 prio 7, EXTI4 prio 8, etc.
6. Deassert all chip selects: `HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET); HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_SET);`
7. Call `ltc6903Init()` -- programs 8.192 MHz, switches SPI mode 0->1
8. Call `diagClkinInit()` -- starts TIM8 counting, enables overflow IRQ
9. Keep SD card init + FatFS mount (production code)
10. Keep USB DCD registration + enumeration wait
11. Wrap Phase 4 SD benchmark sweep in `#if 0` ... `#endif` (retain for reference, skip at runtime)
12. Re-enable EXTI2 at end: `__HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_Pin); HAL_NVIC_EnableIRQ(EXTI2_IRQn);`

Changes in **USER CODE BEGIN 3** (main loop):

- Add `diagClkinPoll();` call

Changes in **USER CODE BEGIN Callback 1** (inside `HAL_TIM_PeriodElapsedCallback`):

- Not needed -- overflow handled directly in `TIM8_UP_IRQHandler` in diag_timers.c

### 6. [Core/Src/debug_ui.c](Core/Src/debug_ui.c) -- no changes needed

The `uiSetClkinHz()` setter (line 157) and the CLKIN display field on row 5 (lines 308-309) already exist and work. `diagClkinPoll()` will call `uiSetClkinHz()` which sets `dirtySlow = 1`, and `uiUpdateFields()` will refresh the display.

## Init Sequence (main.c USER CODE BEGIN 2)

```
MX_GPIO_Init()                  -- CubeMX call (already present)
HAL_NVIC_DisableIRQ(EXTI2_IRQn) -- suppress DRDY during init
MX_GPDMA1_Init()                -- uncommented
MX_USART1_UART_Init()           -- already present
MX_USB_PCD_Init()               -- already present
MX_SPI1_Init()                  -- uncommented
MX_TIM8_Init()                  -- uncommented
NVIC priority overrides          -- new
Deassert CS pins                 -- new
ltc6903Init()                   -- new: Mode0, write 0xCF60, Mode1
diagClkinInit()                -- new: start TIM8 + overflow IRQ
MX_SDMMC1_SD_Init()             -- existing
MX_FATFS_Init()                  -- existing
MX_USBX_Init() + DCD            -- existing
Enumeration wait                 -- existing
Re-enable EXTI2                  -- new
```

## CubeMX Safety Summary


| Item                    | Location                 | Safe?                                          |
| ----------------------- | ------------------------ | ---------------------------------------------- |
| osc_ltc6903.c/h         | New files                | Yes -- not CubeMX-managed                      |
| diag_timers.c/h         | New files                | Yes -- not CubeMX-managed                      |
| TIM8_UP_IRQHandler      | diag_timers.c            | Yes -- CubeMX never generated it               |
| main.c init sequence    | USER CODE BEGIN 2        | Yes -- USER CODE section                       |
| main.c includes         | USER CODE BEGIN Includes | Yes -- USER CODE section                       |
| main.c loop             | USER CODE BEGIN 3        | Yes -- USER CODE section                       |
| NVIC priority overrides | USER CODE BEGIN 2        | Yes -- overrides CubeMX defaults after MspInit |
| SPI1 mode switch        | osc_ltc6903.c (runtime)  | Yes -- not editing spi.c                       |


## Success Criteria (from master plan)

- TIM8 measures **8.192 MHz +/- 0.5%** (8,151,040 -- 8,232,960 Hz) on PC6
- `uiUpdateFields()` shows CLKIN field as `8192xxx Hz`
- Frequency stable (no drift) over 60 s observation
- Serial terminal shows: `LTC6903: init OK, CLKIN=8192xxx Hz`
- Serial terminal shows: `LTC6903: SPI mode switch Mode0->Mode1 complete`

## Naming Convention Compliance

This plan has been retroactively updated to use camelCase naming for functions and locals, and a `g_` prefix plus camelCase for global variables, per [`.cursor/rules/commenting-and-naming.mdc`](../rules/commenting-and-naming.mdc). HAL and CubeMX identifiers are unchanged. When Phase 14 executes, the symbol names documented here (for example `ltc6903Init`, `diagClkinInit`, `diagClkinPoll`, `diagClkinGetHz`, `g_tim8OvfCount`, `uiSetClkinHz`, `txData`) serve as the target reference for matching firmware and documentation.

