---
name: SYSCLK 75 to 250 MHz
overview: Increase STM32H562 SYSCLK from 75 MHz (VOS3) to 250 MHz (VOS0) by reconfiguring PLL1, voltage scaling, flash latency, and adjusting all affected peripheral prescalers while keeping SPI1/SDMMC kernel clocks unchanged.
todos:
  - id: pll-config
    content: "Update PLL1: PLLN=31, FRACN=2048, PLLQ=10 for VCO=500MHz, SYSCLK=250MHz, PLL1Q=50MHz"
    status: completed
  - id: vos-flash
    content: Set VOS0, FLASH_LATENCY_5, PROGRAMMING_DELAY_2 in SystemClock_Config()
    status: completed
  - id: vos-errata
    content: Verify silicon revision vs errata ES0565 2.2.18; REV_ID=0x1007 not affected
    status: completed
  - id: icache-enable
    content: Uncomment MX_ICACHE_Init() in main.c to recover flash performance at 5 wait states
    status: completed
  - id: adc-prescaler
    content: Change ADC_CLOCK_ASYNC_DIV1 to DIV4 in adc.c (for when ADC1 is enabled)
    status: completed
  - id: ioc-update
    content: Update .ioc file clock settings to match (prevents CubeMX overwrite)
    status: completed
  - id: boot-test
    content: "Boot test on hardware: verified SYSCLK=250MHz, SPI1=12.5MHz, USB CDC, SD 59GB, ADC DMA 0 miss"
    status: completed
isProject: false
---

# SYSCLK Upgrade: 75 MHz to 250 MHz

## Current Clock Tree

```
HSI = 64 MHz
  |
  PLLM = 4 --> VCO input = 16 MHz
  |
  PLLN = 9, FRACN = 3072 --> VCO = 150 MHz
  |
  +-- PLLP = 2 --> SYSCLK = 75 MHz (HCLK, APB1, APB2, APB3 all /1 = 75 MHz)
  +-- PLLQ = 3 --> PLL1Q  = 50 MHz (SPI1 kernel, SDMMC1 kernel)
  +-- PLLR = 2 --> PLL1R  = 75 MHz (unused)

Voltage: VOS3 (VCORE = 1.0V)
Flash:   LATENCY_3, PROGRAMMING_DELAY_1
ICACHE:  Disabled
```

## Target Clock Tree

```
HSI = 64 MHz
  |
  PLLM = 4 --> VCO input = 16 MHz (unchanged, VCI range 3: 8-16 MHz)
  |
  PLLN = 31, FRACN = 2048 --> VCO = 500 MHz (WIDE range: 192-836 MHz)
  |
  +-- PLLP = 2  --> SYSCLK = 250 MHz
  +-- PLLQ = 10 --> PLL1Q  = 50 MHz  (UNCHANGED -- SPI1 and SDMMC1 stay identical)
  +-- PLLR = 2  --> PLL1R  = 250 MHz (available if needed)

Voltage: VOS0 (VCORE = 1.35V)
Flash:   LATENCY_5, PROGRAMMING_DELAY_2
ICACHE:  Enable (strongly recommended at 5 wait states)
```

**Key design choice:** By setting PLLQ = 10, the SPI1 and SDMMC1 kernel clocks stay at 50 MHz. This means zero changes to SPI prescalers, SDMMC clock dividers, or SPI transfer timing. The ADS131M02 DMA path is completely unaffected at the bus level.

## Changes Required

### 1. `SystemClock_Config()` in [main.c](Core/Src/main.c) (lines 309-364)

```c
// BEFORE:
__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
// PLL: PLLM=4, PLLN=9, FRACN=3072, PLLP=2, PLLQ=3, PLLR=2
// HAL_RCC_ClockConfig(..., FLASH_LATENCY_3)
// __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_1)

// AFTER:
__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
// PLL: PLLM=4, PLLN=31, FRACN=2048, PLLP=2, PLLQ=10, PLLR=2
// HAL_RCC_ClockConfig(..., FLASH_LATENCY_5)
// __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2)
```

### 2. Enable ICACHE in [main.c](Core/Src/main.c) (line 117)

Uncomment `MX_ICACHE_Init()`. At 5 flash wait states, ICACHE recovers near-zero-wait-state performance for sequential code execution.

### 3. ADC1 Prescaler in [adc.c](Core/Src/adc.c) (line 45)

```c
// BEFORE: ADC_CLOCK_ASYNC_DIV1 --> 250 MHz (EXCEEDS ADC max of ~80 MHz)
// AFTER:  ADC_CLOCK_ASYNC_DIV4 --> 62.5 MHz (safe)
```

Note: ADC1 is currently commented out in `main.c` (`MX_ADC1_Init()`), but fix the init code so it's correct when enabled.

### 4. No Change Required (peripherals that auto-adapt or stay on same kernel clock)

- **SPI1:** Kernel = PLL1Q = 50 MHz (unchanged), prescaler /4 = 12.5 MHz SCK (unchanged)
- **SDMMC1:** Kernel = PLL1Q = 50 MHz (unchanged), ClockDiv values produce same SD bus speeds
- **SPI2:** Kernel = PLL2P = 20 MHz (independent of PLL1, unchanged)
- **USART1:** Kernel = PCLK2. HAL auto-computes BRR from kernel clock in `MX_USART1_UART_Init()`. Since PCLK2 changes to 250 MHz, the BRR will be recalculated at init time -- 921600 baud is achievable from 250 MHz (divider = ~271). **No code change needed.**
- **TIM6 (HAL tick):** `HAL_InitTick()` auto-computes prescaler from PCLK1. **No code change.**
- **TIM3/TIM8:** External clock mode (counting edges, not driven by SYSCLK). **No change.**
- **USB:** Source = HSI48. **No change.**
- **GPDMA:** Bus clock scales automatically. Throughput improves. **No change.**
- **DWT CYCCNT:** `SystemCoreClock` is updated by HAL after clock config. All cycle-based calculations (e.g., `SystemCoreClock / 10` for 100 ms timeout) scale automatically.

## Blockers / Gotchas

### BLOCKER 1: Errata ES0565 Section 2.2.18 — Flash Read Failure During VOS Transition

**Problem:** On STM32H562 Rev A/B silicon, flash reads may fail in VOS2 and VOS1 ranges. When transitioning from VOS3 to VOS0, the hardware passes through VOS2 and VOS1 internally. If code is executing from flash during this transition, it can crash.

**Impact:** `SystemClock_Config()` runs from flash. The `__HAL_PWR_VOLTAGESCALING_CONFIG(VOS0)` call inside it can cause a HardFault.

**Resolution options:**
- **(A) Check silicon revision.** Later revisions (columns 3-4 in errata table show "-") may not have this issue. Read `DBGMCU->IDCODE` at startup to check `REV_ID`.
- **(B) Execute VOS switch from SRAM.** Mark the VOS transition function with `__attribute__((section(".RamFunc")))` or `__RAM_FUNC`. Copy to SRAM at startup via scatter-load. Call from `SystemClock_Config()` before enabling PLL.
- **(C) Set flash latency conservatively high before VOS switch.** Set `FLASH_LATENCY_5` before touching VOS, then do the transition. This doesn't fully solve the errata but reduces risk. Not guaranteed safe by ST.
- **(D) Boot directly into VOS0.** If the default reset state allows VOS0 configuration before PLL activation (while still at 64 MHz HSI), the flash latency of 5 is more than sufficient for 64 MHz. The transition happens before the PLL is enabled.

**Recommended approach:** Option (D) -- configure VOS0 and high flash latency **first** (while still at 64 MHz HSI), **then** enable PLL at 250 MHz. This is the standard CubeMX-generated pattern: set VOS, wait VOSRDY, set flash latency, configure PLL, switch SYSCLK. At 64 MHz, even VOS1/VOS2 intermediate states should handle flash reads (max freq for VOS2 is 150 MHz). Verify with a boot test on actual silicon.

### BLOCKER 2: Flash Latency Must Be Set BEFORE Clock Increase

**Problem:** If SYSCLK increases before flash latency is updated, the CPU fetches instructions faster than flash can deliver them -- instant crash.

**Resolution:** The standard `HAL_RCC_ClockConfig()` handles this correctly: it checks whether the new HCLK is higher than current, and if so, sets the new (higher) latency **first**, then switches the clock. This is already the HAL's behavior. No special handling needed as long as you pass the correct latency to `HAL_RCC_ClockConfig()`.

### GOTCHA 3: APB Prescalers at 250 MHz

**Problem:** Currently all APB dividers are /1 (APB1 = APB2 = APB3 = HCLK). At 250 MHz, this means all APB buses run at 250 MHz. Per the STM32H562 datasheet, APB buses can run up to 250 MHz in VOS0 -- so /1 is technically valid.

**Impact:** Timer clocks: when APB prescaler = /1, timer clock = APB clock (no 2x multiplier). So TIM6 tick prescaler auto-adjusts correctly via `HAL_InitTick()`.

**Resolution:** No change needed if all APBs support 250 MHz in VOS0 (they do per datasheet). If any peripheral on APB has trouble, consider APB2 or APB3 divider to /2.

### GOTCHA 4: ADC Maximum Kernel Clock

**Problem:** ADC1 is sourced from HCLK with DIV1 prescaler. At 250 MHz, this exceeds the ADC maximum clock (~80 MHz for 12-bit resolution on STM32H5).

**Resolution:** Change `ADC_CLOCK_ASYNC_DIV1` to `ADC_CLOCK_ASYNC_DIV4` (62.5 MHz). ADC1 is currently disabled (`MX_ADC1_Init()` is commented out), so this is not a blocker today but must be fixed before enabling ADC1.

### GOTCHA 5: Power Consumption Increase

**Problem:** VOS0 at 250 MHz draws significantly more current than VOS3 at 75 MHz. Datasheet typical: VOS3 Run = ~15 mA, VOS0 Run = ~45-60 mA (varies with peripherals active).

**Resolution:** Verify board power supply can deliver the additional current. Check voltage regulator thermal limits. No code change -- hardware verification.

### GOTCHA 6: ICACHE Should Be Enabled

**Problem:** At 5 flash wait states, every instruction cache miss costs 5 extra cycles. Code execution performance without ICACHE will be significantly impacted, especially for ISR latency.

**Resolution:** Uncomment `MX_ICACHE_Init()` in `main.c`. The ICACHE is already configured in [icache.c](Core/Src/icache.c). This is low-risk -- the H5 ICACHE is well-tested.

### GOTCHA 7: ADS131M02 DMA ISR Timing Impact

**Problem:** The SPI transfer time is unchanged (12.5 MHz SCK = 7.7 us), but CPU cycles per microsecond increases from 75 to 250. The `adsFastStart()` and `adsFastComplete()` ISR code will execute ~3.3x faster in CPU cycles, but the same wall-clock time for SPI transfers.

**Impact:** This is actually **beneficial** -- more CPU time available between DRDY edges. The 15.625 us budget now has ~10+ us of CPU headroom (was ~8 us). The guard NOP loop in the SPI teardown (16 NOPs) becomes shorter in wall-clock time (~64 ns vs ~213 ns) -- may need to increase NOP count if used.

**Resolution:** Verify DMA operation at 250 MHz. Consider increasing NOP guard count from 16 to ~53 to maintain the same wall-clock guard delay (~213 ns). Or switch to a DWT-based delay.

### GOTCHA 8: CubeMX Regeneration

**Problem:** If CubeMX regenerates code, it will overwrite `SystemClock_Config()` with whatever is in the `.ioc` file. The `.ioc` currently has 75 MHz settings.

**Resolution:** Either:
- **(A)** Update the `.ioc` file through CubeMX GUI (Clock Configuration tab) to 250 MHz / VOS0, then regenerate. This is the cleanest path.
- **(B)** Edit `SystemClock_Config()` manually in USER CODE sections and protect from regeneration.

**Recommended:** Option (A) -- use CubeMX to change clock settings, which auto-calculates flash latency, VOS, and programming delay.

### GOTCHA 9: SPI1 Prescaler in Register-Level DMA Code

**Problem:** The ADS131M02 DMA hot path writes SPI1 registers directly (not through HAL). The baud rate prescaler is in `SPI1->CFG1`, set by `MX_SPI1_Init()` via HAL. Since PLL1Q stays at 50 MHz and the prescaler stays at /4, the CFG1 value is unchanged.

**Resolution:** No change needed, but verify by reading `SPI1->CFG1` after init at 250 MHz.

### GOTCHA 10: HSI48 USB Clock Accuracy

**Problem:** HSI48 is a free-running 48 MHz RC oscillator used for USB. Its accuracy depends on temperature and voltage. At VOS0, VCORE changes from 1.0V to 1.35V, which could slightly shift HSI48 frequency.

**Resolution:** The CRS (Clock Recovery System) can trim HSI48 using USB SOF packets. If USB enumeration issues appear, enable CRS. Currently, HSI48 is used directly without CRS -- monitor USB stability after the change.

## Implementation Approach

The safest method is to make the change through CubeMX, which ensures all register values are consistent:

1. Open `H562_Loadcell_Datalogger.ioc` in CubeMX
2. Clock Configuration tab: set SYSCLK target to 250 MHz
3. CubeMX will auto-solve: VOS0, PLLM=4, PLLN=31 (or similar), PLLQ=10, FLASH_LATENCY_5
4. Regenerate code
5. Re-apply all USER CODE customizations
6. Uncomment `MX_ICACHE_Init()`
7. Fix ADC prescaler if/when ADC1 is enabled
8. Boot-test on hardware

If manual edit is preferred (no CubeMX regeneration), change only `SystemClock_Config()` -- 4 values: VOS, PLL N/FRACN/Q, flash latency, programming delay.

## Naming Convention Compliance

This plan has been retroactively updated so references to project application code use **camelCase** function names per [`.cursor/rules/commenting-and-naming.mdc`](../rules/commenting-and-naming.mdc). HAL-, CubeMX-, and third-party API names (for example `SystemClock_Config()`, `MX_ICACHE_Init()`, `HAL_RCC_ClockConfig()`) are left unchanged.
