---
name: Bridge ADS131M02 Driver Gaps
overview: Refactor the ADS131M02 driver to align with the ideal architecture from two_cents.md, addressing gaps in DRDY mode, SPI teardown ordering, ring buffer, ISR instrumentation, health telemetry, and error handling. Prioritize changes testable at 75 MHz SYSCLK.
todos:
  - id: strip-prints
    content: "Phase A-0: Strip UART prints to bare minimum, disable CDC output, remove register dumps and verbose start/stop messages"
    status: pending
  - id: spi-idle-helper
    content: "Phase A-1: Extract adsSpiForceCleanIdle() helper; use in setup, stop, and hard-cleanup"
    status: pending
  - id: fix-stop
    content: "Phase A-2: Fix ads131m02StopContinuous — disable EXTI2+CH1 NVIC first, IDLEF waits, force-clean-idle, CS HIGH"
    status: pending
  - id: spi-teardown
    content: "Phase A-3: Fix adsFastComplete() and skip-frame path — add DSB+guard after TXC"
    status: pending
  - id: dma-error-path
    content: "Phase A-4: Add DMA error handling — clear busy, increment dmaErrorCount on non-TCF IRQ"
    status: pending
  - id: stats-expansion
    content: "Phase A-5: Expand adsDmaStats_t with dmaStartCount, dmaErrorCount, maxExtiCycles, maxDmaCycles, busy, running"
    status: pending
  - id: isr-instrumentation
    content: "Phase A-6: Add DWT cycle instrumentation to both ISRs, track maxExtiCycles/maxDmaCycles"
    status: pending
  - id: symbolic-mode
    content: "Phase A-7: Replace opaque ADS_MODE_INIT with symbolic per-field defines in header"
    status: pending
  - id: fix-stale-comment
    content: "Phase A-8: Fix stale CSAR/CDAR comment at line 261 of adc_ads131m02.c"
    status: pending
  - id: drdy-pulse-mode
    content: "Phase B-1: Switch default DRDY mode to pulse — A/B test already validated"
    status: pending
  - id: telemetry-update
    content: "Phase B-2: Update main.c telemetry to print new counters (errors, max cycles)"
    status: pending
  - id: ring-buffer
    content: "Phase C-1: Add adsSample_t ring buffer (1024 entries) with ISR push / foreground pop"
    status: pending
  - id: health-telemetry
    content: "Phase C-2: Add adsHealthSnapshot() for periodic HW vs SW DRDY comparison"
    status: pending
  - id: simplify-start-stop
    content: "Phase D-1: Evaluate whether retry loop and manual DMA test can be simplified"
    status: pending
isProject: false
---

# Bridge ADS131M02 Driver to Ideal Design

## Serial Debug Baseline (2026-04-12)

Full capture in [Serisl_Debug.txt](Troubleshooting/Serisl_Debug.txt). System is **stable** -- no HardFaults, no hangs. All tests complete, continuous telemetry runs 100+ seconds.

### Measured performance at 75 MHz SYSCLK

| Metric | Level (DRDY_FMT=0) | Pulse (DRDY_FMT=1) |
|--------|---------------------|---------------------|
| DRDY_SW/s | 85,775 | 51,133 |
| DRDY_HW/s | 85,805 | 51,148 |
| DMA completions/s | 47,926 | 38,192 |
| miss/s | 37,777 | 12,905 |
| Miss rate | 44% | 25% |
| DMA vs 64k target | 74.9% | 59.7% |

- Average DMA cycle time (level mode) = 1/47,470 = **21.1 us** (vs 15.625 us DRDY period)
- SPI wire time: 12B x 8 / 18.75 MHz = **5.12 us**
- ISR + register overhead: **~16 us** (the bottleneck at 75 MHz)
- DRDY_HW == DRDY_SW: no EXTI misses, hardware and software agree perfectly
- USB CDC stall observed once (~620 frozen lines), self-recovered, not fatal

### Why level mode shows 85k/s DRDY instead of 64k

In level mode DRDY stays low until data is read. When we are busy with a DMA transfer and the next conversion completes, DRDY bounces high then back low. EXTI catches these extra edges, inflating DRDY_SW to 85k/s. Each spurious trigger wastes ISR time checking `adsDmaBusy`.

### Why pulse mode shows only 51k/s DRDY instead of 64k

In pulse mode, if data is not read before the next conversion, the ADC **does not emit the next pulse** (ADS131M02 spec). Missed reads cause permanently lost conversions. Hence 51k/s < 64k.

### Bottleneck conclusion

Neither mode reaches zero-miss at 75 MHz because the total ISR + DMA cycle time (~21 us) exceeds the 15.625 us DRDY period. To reach zero-miss, the cycle time must drop below 15.625 us. Two levers:

1. **Reduce wasted ISR overhead at 75 MHz** (this plan) -- switch to pulse mode, fix ads131m02StopContinuous, add guard delays
2. **Increase SYSCLK to 250 MHz** (future, if needed) -- cuts ISR overhead ~3.3x

---

## Gap Analysis

Comparing the current driver ([adc_ads131m02.c](Core/Src/adc_ads131m02.c), [adc_ads131m02.h](Core/Inc/adc_ads131m02.h)) against the ideal design in [two_cents.md](Troubleshooting/two_cents.md).

### Gap 1: `ads131m02StopContinuous()` does not gate interrupts

**Current** ([adc_ads131m02.c](Core/Src/adc_ads131m02.c) line 586):
```c
adsDmaStop = 1;
while (adsDmaBusy) { ... timeout ... }
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
// no EXTI2 disable, no CH1 NVIC disable, no IDLEF wait, no force-flush
```

**Ideal** ([two_cents.md](Troubleshooting/two_cents.md) line 356):
```c
NVIC_DisableIRQ(EXTI2_IRQn);
NVIC_DisableIRQ(GPDMA1_Channel1_IRQn);
// ... disable DMA, IDLEF waits, force_clean_idle, CS HIGH ...
```

**Impact:** With level mode, EXTI2 fires 83k/s even after `adsDmaStop=1` -- each call enters the ISR, reads flags, checks `adsDmaStop`, and returns. Wasted cycles during every start/stop transition (polarity test does 60 stop/start cycles).

**Fix:** Disable EXTI2 and CH1 NVIC first, then DMA, then IDLEF wait, then `adsSpiForceCleanIdle()`, then CS HIGH, then clear busy. Re-enable NVIC sources in `ads131m02StartContinuous()`.

### Gap 2: SPI teardown missing DSB + guard delay

**Current** ([adc_ads131m02.c](Core/Src/adc_ads131m02.c) line 355):
```c
while (!(SPI1->SR & SPI_SR_TXC)) __NOP();
GPIOA->BSRR = ADC_CS_Pin;          // CS HIGH immediately
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
CLEAR_BIT(SPI1->CFG1, ...DMAEN);
```

**Ideal:**
```c
while (!(SPI1->SR & SPI_SR_TXC)) {}
__DSB();
for (volatile int i = 0; i < 16; ++i) __NOP();
GPIOA->BSRR = ADC_CS_Pin;
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
CLEAR_BIT(SPI1->CFG1, ...DMAEN);
```

ST AN5543 and H562 errata warn that disabling SPI too quickly after EOT can truncate the last clock edge. The `__DSB()` ensures the TXC read has propagated through the bus fabric; the 16 NOPs give a ~0.2 us guard at 75 MHz.

**RM0481 constraint preserved:** CFG1 write stays after SPE=0. Only the DSB + guard are inserted before CS HIGH.

Same fix needed in the skip-frame path (line 425).

### Gap 3: No DMA error handling in ISR

**Current** ([adc_ads131m02.c](Core/Src/adc_ads131m02.c) line 420):
```c
if (!(GPDMA1_Channel1->CSR & DMA_CSR_TCF))
    return 0;   // busy stays 1 forever -- deadlock
```

**Ideal:** If IRQ fires for a non-TCF reason, clear all flags, increment `dmaErrorCount`, clear `busy`.

### Gap 4: No ISR cycle instrumentation

Both ISRs lack `DWT->CYCCNT` timing. Without this, we cannot measure the actual overhead to know if 75 MHz is sufficient or 250 MHz is required.

### Gap 5: SPI flush logic duplicated

The `SPE 1->0` flush + `IFCR` clear appears inline in `adsDmaSetup()` (line 319), `ads131m02StartContinuous()` hard cleanup (line 538), and should also appear in `ads131m02StopContinuous()`. Extract to a single `adsSpiForceCleanIdle()` helper.

### Gap 6: Stats struct missing fields

`adsDmaStats_t` lacks: `dmaStartCount`, `dmaErrorCount`, `maxExtiCycles`, `maxDmaCycles`, `busy`, `running`.

### Gap 7: Opaque MODE literal

`ADS_MODE_INIT = 0x0110u` -- should be composed from symbolic field defines.

### Gap 8: DRDY level mode (validated by A/B test)

A/B test confirms pulse mode produces 30% fewer spurious interrupts. Switch default once all correctness fixes are in place.

### Gap 9: Stale comment

Line 261: "CSAR/CDAR hold their original values (not modified)" is empirically wrong on STM32H5 GPDMA NORMAL mode. Already fixed in code (CDAR/CSAR reload per frame), but comment is misleading.

### Gap 10: Ring buffer

No sample buffering. ISR writes `ch0Latest`/`ch1Latest` directly. Not atomic for foreground reads. Deferred to Phase C since it does not affect miss rate.

### Gap 11: Health telemetry

No periodic HW vs SW DRDY comparison from foreground. Deferred to Phase C.

---

## Crash Context Constraints (MUST be respected)

From [PHASE7_DMA_CRASH_CONTEXT.md](Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md):

- **RM0481 CFG1 rule:** SPI CFG1/CFG2 can only be written when SPE=0 (Section 20)
- **SPE flush rule:** `CLEAR_BIT(SPE)` when SPE is already 0 does NOT flush FIFOs -- must force a 1-to-0 transition (Section 15)
- **GPDMA CSAR/CDAR drift:** In NORMAL mode, CSAR/CDAR are updated to past-end-of-buffer after transfer -- must reload every frame (Section 21)
- **ISR/polling race:** Never poll TCF with the TCIE NVIC enabled -- ISR consumes TCF first (Section 18)
- **IDLEF before reprogram:** Must wait for `CSR.IDLEF=1` after clearing `CCR.EN` before writing CSAR/CDAR/CBR1 (Section 19)
- **HAL DMA handler interference:** Stale HAL state + register-level transfers = corruption -- intercept all DMA IRQs before HAL (Section 17)

---

## Implementation Plan (75 MHz first)

### Phase A -- Correctness and Observability (no functional changes)

All items are safe, incremental, and can be verified at 75 MHz with the existing serial telemetry.

**A-0. Strip prints to bare minimum, disable CDC output**

`printf` goes to UART1 via blocking `HAL_UART_Transmit` (921600 baud, ~10.9 us/char). At 64 kSPS, every 1 ms of UART blocking = ~64 missed conversions. `cdcPrintf` is non-blocking but still burns CPU in `cdcPoll()`.

Files and changes:

**[Core/Src/adc_ads131m02.c](Core/Src/adc_ads131m02.c):**

- **Remove** all `adsDumpDmaRegs()` calls in `ads131m02StartContinuous()` (lines 522, 528, 562) -- these were diagnostic, now proven working. Keep the function itself for future debug.
- **Remove** verbose prints: `"adsDmaSetup..."`, `"setup done"`, `"enabling DRDY-triggered DMA..."` (lines 517, 519, 564)
- **Remove** `"[DMA-TEST] starting single manual transfer..."` (line 468)
- **Remove** the RX hex dump loop in `adsManualDmaTest()` (lines 503-506) -- keep only the CH0/CH1 summary
- **Keep** one-line DMA test result: `"[DMA-TEST] OK CH0=%+ld CH1=%+ld"` or `"[DMA-TEST] FAIL"` 
- **Keep** retry print: `"retry %d"` (line 515) -- important for diagnosing intermittent failures
- **Keep** fatal: `"DMA TEST FAILED after 3 attempts"` (line 574)
- **Keep** all `ads131m02Init()` prints (one-time at boot, not in hot path)
- **Keep** polarity test header/summary prints but **remove** per-window `[TEST N]` lines (line 691) during the test -- OR gate them behind a flag. These 60 lines of output during polarity test cost ~66 ms of UART blocking.

**[Core/Src/debug_ui.c](Core/Src/debug_ui.c):**

- **Disable** `ui_uart_dump()` entirely: return immediately at top. The 6-line UART dump costs ~5.5 ms/s of blocking. ADC telemetry line in `main.c` is sufficient.
- **Disable** `cdcPrintf` calls: either return immediately in `cdcPrintf()`, or skip `uiDrawPanel()` / `uiUpdateFields()` calls in main loop.

**[Core/Src/main.c](Core/Src/main.c):**

- **Remove** `"[MAIN] calling ads131m02StartContinuous()..."` (line 230)
- **Remove** `"[MAIN] 500 ms: ..."` print (line 244) -- replaced by the 1-second telemetry
- **Keep** the 1-second ADC telemetry line (line 294) -- this is the primary debug output
- **Condense** the ADC telemetry to one short line: `"ADC: sw=%lu hw=%lu ok=%lu miss=%lu err=%lu exti=%lu dma=%lu\r\n"`
- **Comment out** `cdcPoll()` call in main loop (line 271) -- saves CDC state machine overhead

Net effect: ~12 ms/s of UART blocking reduced to ~1 ms/s. CDC overhead eliminated entirely.

**A-1. Extract `adsSpiForceCleanIdle()` helper**

```c
static void adsSpiForceCleanIdle(void)
{
    CLEAR_BIT(SPI1->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
    SET_BIT(SPI1->CR1, SPI_CR1_SPE);
    CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
    SPI1->IFCR = SPI_IFCR_ALL;
}
```

Replace inline flush logic in `adsDmaSetup()` (lines 319-324) and `ads131m02StartContinuous()` hard cleanup (lines 538-542). Add call in `ads131m02StopContinuous()`.

**A-2. Fix `ads131m02StopContinuous()`**

New implementation:

```c
void ads131m02StopContinuous(void)
{
    adsDmaStop = 1;
    NVIC_DisableIRQ(EXTI2_IRQn);
    NVIC_DisableIRQ(GPDMA1_Channel1_IRQn);

    CLEAR_BIT(dma_rx->CCR, DMA_CCR_EN);
    CLEAR_BIT(dma_tx->CCR, DMA_CCR_EN);
    while (!(dma_rx->CSR & DMA_CSR_IDLEF)) __NOP();
    while (!(dma_tx->CSR & DMA_CSR_IDLEF)) __NOP();

    adsSpiForceCleanIdle();
    GPIOA->BSRR = ADC_CS_Pin;
    dma_rx->CFCR = DMA_CFCR_ALL;
    dma_tx->CFCR = DMA_CFCR_ALL;

    adsDmaBusy = 0;
    hspi1.State = HAL_SPI_STATE_READY;
    handle_GPDMA1_Channel0.State = HAL_DMA_STATE_READY;
    handle_GPDMA1_Channel1.State = HAL_DMA_STATE_READY;
}
```

Key changes: disable EXTI2+CH1 NVIC first (stops 83k/s wasted ISR entries), add IDLEF waits, call `adsSpiForceCleanIdle()`, assert CS HIGH. Remove the HAL_GetTick busy-wait -- NVIC disable guarantees no new transfers start.

**A-3. Fix `adsFastComplete()` and skip-frame path**

Insert `__DSB()` + 16-NOP guard between TXC wait and CS HIGH. Apply to both `adsFastComplete()` (line 357) and the skip-frame path in `adsFastDmaCompleteHandler()` (line 427).

```c
while (!(SPI1->SR & SPI_SR_TXC)) __NOP();
__DSB();
for (volatile int i = 0; i < 16; ++i) __NOP();
GPIOA->BSRR = ADC_CS_Pin;
// rest unchanged
```

**A-4. DMA error handling**

In `adsFastDmaCompleteHandler()`, if TCF is not set:

```c
if (!(GPDMA1_Channel1->CSR & DMA_CSR_TCF)) {
    dma_rx->CFCR = DMA_CFCR_ALL;
    dma_tx->CFCR = DMA_CFCR_ALL;
    adsStats.dmaErrorCount++;
    adsDmaBusy = 0;
    return 1;
}
```

**A-5. Expand `adsDmaStats_t`**

Add to struct in [adc_ads131m02.h](Core/Inc/adc_ads131m02.h):

```c
volatile uint32_t dmaStartCount;
volatile uint32_t dmaErrorCount;
volatile uint32_t maxExtiCycles;
volatile uint32_t maxDmaCycles;
```

**A-6. ISR DWT instrumentation**

Wrap `adsFastDrdyHandler()` and `adsFastDmaCompleteHandler()` with `DWT->CYCCNT` delta capture. Track `maxExtiCycles` and `maxDmaCycles` in stats.

**A-7. Symbolic MODE defines**

In [adc_ads131m02.h](Core/Inc/adc_ads131m02.h), replace:

```c
#define ADS_MODE_INIT  0x0110u
```

With:

```c
#define ADS_MODE_WL_24         (1u << 8)
#define ADS_MODE_TIMEOUT_EN    (1u << 4)
#define ADS_MODE_DRDY_PULSE    (1u << 0)
#define ADS_MODE_DRDY_LEVEL    (0u << 0)
#define ADS_MODE_DRDY_PUSHPULL (0u << 1)
#define ADS_MODE_DRDY_MOST_LAG (0u << 2)

#define ADS_MODE_INIT  (ADS_MODE_WL_24 | ADS_MODE_TIMEOUT_EN | ADS_MODE_DRDY_LEVEL)
```

**A-8. Fix stale comment**

Line 261 of [adc_ads131m02.c](Core/Src/adc_ads131m02.c): change "CSAR/CDAR hold their original values (not modified)" to "CSAR/CDAR advance past end of buffer (must reload)".

### Phase B -- Performance at 75 MHz

**B-1. Switch to pulse mode**

Change `ADS_MODE_INIT` to use `ADS_MODE_DRDY_PULSE`:

```c
#define ADS_MODE_INIT  (ADS_MODE_WL_24 | ADS_MODE_TIMEOUT_EN | ADS_MODE_DRDY_PULSE)
```

Expected effects:
- DRDY_SW drops from ~85k/s to ~64k/s (no spurious re-assertions)
- ~20k fewer ISR entries/s, each saving ~0.5 us = ~10 us/s saved
- Miss rate should improve from 44% -- exact improvement depends on DWT cycle measurements from Phase A

**B-2. Update main.c telemetry**

Print new counters in the 1-second ADC telemetry line:

```
ADC: SW=83k HW=83k DMA=47k miss=35k err=0 exti_max=123 dma_max=456
```

### Phase C -- Data Flow (deferred, does not affect miss rate)

**C-1. Ring buffer** -- 1024-entry `adsSample_t` ring, ISR push, foreground pop.
**C-2. Health telemetry** -- `adsHealthSnapshot()` comparing TIM3 count vs software counters.

### Phase D -- Cleanup (deferred until stability confirmed)

**D-1. Simplify start/stop** -- evaluate whether retry loop and manual DMA test can be removed once all Phase A fixes are verified.

---

## Files to Modify

- [Core/Inc/adc_ads131m02.h](Core/Inc/adc_ads131m02.h) -- symbolic MODE defines, stats struct expansion, new API
- [Core/Src/adc_ads131m02.c](Core/Src/adc_ads131m02.c) -- bulk of changes: helper extraction, stop fix, teardown fix, error handling, ISR instrumentation
- [Core/Src/stm32h5xx_it.c](Core/Src/stm32h5xx_it.c) -- CH1 handler: error path now returns 1 (handler consumed the IRQ)
- [Core/Src/main.c](Core/Src/main.c) -- telemetry print update

## Expected Outcome After Phase A+B

- Miss rate improvement at 75 MHz (measurable via DWT instrumentation)
- `ads131m02StopContinuous()` no longer burns 83k ISR entries/s during transitions
- SPI teardown is errata-safe
- DMA errors are caught instead of causing busy-deadlock
- Exact ISR overhead measured -- this tells us whether 250 MHz SYSCLK is needed or whether further ISR optimization at 75 MHz is sufficient

## Naming Convention Compliance

This plan has been retroactively updated to use camelCase naming for application functions and for local/static variable identifiers where they appear in code excerpts and task text, per [.cursor/rules/commenting-and-naming.mdc](.cursor/rules/commenting-and-naming.mdc). HAL, CMSIS, and CubeMX-generated symbols (for example `NVIC_DisableIRQ`, `HAL_SPI_STATE_READY`, register structs) are unchanged.
