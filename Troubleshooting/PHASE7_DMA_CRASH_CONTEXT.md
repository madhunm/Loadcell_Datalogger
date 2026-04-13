# Phase 7: ADS131M02 64 kSPS DMA Continuous Capture — Troubleshooting Context

**Date:** 2026-04-12  
**Status:** System hangs/crashes immediately after enabling register-level DMA  
**MCU:** STM32H562RGT6 (Cortex-M33, SYSCLK = 75 MHz)

---

## 1. Goal

Capture every ADS131M02 DRDY event (64 kSPS) via EXTI2-triggered SPI1 DMA transfers with zero sample loss.

**Timing budget:**
- DRDY period: 1/64000 = 15.625 µs
- SPI transfer: 12 bytes × 8 bits / (75 MHz / 4) = 5.12 µs
- Available ISR overhead: ~10 µs

---

## 2. Why Register-Level DMA

`HAL_SPI_TransmitReceive_DMA()` takes ~40–47 µs per call on the STM32H5 GPDMA, far exceeding the 15.625 µs period. This was measured in the first test run (see Section 6). Register-level GPDMA restarts target ~3 µs total ISR overhead.

---

## 3. Hardware Connections

| Signal     | MCU Pin | Peripheral          | Notes                          |
|------------|---------|----------------------|--------------------------------|
| SPI1_SCK   | PA5     | ADS131M02 + LTC6903 | Shared SPI bus                 |
| SPI1_MISO  | PA6     | ADS131M02            | DOUT                           |
| SPI1_MOSI  | PA7     | ADS131M02 + LTC6903 | DIN                            |
| ADC_CS     | PA4     | ADS131M02            | Active low, GPIO-controlled    |
| LTC_CS     | PA1     | LTC6903              | Active low, GPIO-controlled    |
| ADC_DRDY   | PB2     | ADS131M02            | Falling edge → EXTI2           |
| ADC_Reset  | PA3     | ADS131M02            | Active low hardware reset      |
| DRDY_Reader| PB4     | TIM3_CH1 (ext clk)   | HW DRDY edge counter           |
| LTC_CLK    | PC6     | TIM8_CH1 (ext clk)   | HW CLKIN frequency counter     |

---

## 4. Peripheral Configuration (CubeMX + Runtime)

### SPI1
- **Mode:** Master, Full Duplex, Mode 1 (CPOL=0, CPHA=1)
- **Data Size:** 8-bit
- **Baud Rate:** SYSCLK/4 = 18.75 MHz (from PLL1Q via `RCC_SPI1CLKSOURCE_PLL1Q`)
- **FIFO Threshold:** 1 data
- **NSS:** Software (CS managed via GPIO)
- **Frame:** 4 × 24-bit words = 12 bytes (`ADS_FRAME_BYTES`)
- **NVIC:** SPI1_IRQn enabled at priority 0 (CubeMX default)

### GPDMA1
- **Channel 0 (TX):** SPI1_TX, Memory-to-Peripheral, SRC_INC, DST_FIXED, BYTE width
- **Channel 1 (RX):** SPI1_RX, Peripheral-to-Memory, SRC_FIXED, DST_INC, BYTE width
- **Mode:** DMA_NORMAL (single block, non-circular)
- **Priority:** LOW_PRIORITY_HIGH_WEIGHT on both
- **TransferEventMode:** DMA_TCEM_BLOCK_TRANSFER
- **NVIC:** GPDMA1_Channel0_IRQn priority 0, GPDMA1_Channel1_IRQn priority 0

### EXTI2
- **Source:** PB2 (ADC_DRDY)
- **Trigger:** Falling edge
- **NVIC:** Priority 0
- **Enabled at:** main.c line 224 (`HAL_NVIC_EnableIRQ(EXTI2_IRQn)`)

### NVIC Priority Summary
| IRQ                   | Priority | Notes                         |
|-----------------------|----------|-------------------------------|
| EXTI2 (DRDY)         | 0        | Triggers DMA transfer         |
| GPDMA1_Channel0 (TX) | 0        | TX DMA complete               |
| GPDMA1_Channel1 (RX) | 0        | RX DMA complete (TCIE)        |
| SPI1                  | 0        | Should NOT fire (IER=0)       |
| TIM3                  | 8        | DRDY HW edge counter overflow |
| TIM8_UP               | 10       | CLKIN counter overflow         |
| SDMMC1                | 5        | SD card                       |

---

## 5. Register-Level DMA Architecture

### DMA_Channel_TypeDef Layout (from stm32h562xx.h)
```
Offset  Register  Description
0x00    CLBAR     Linked-list base address (0 for normal mode)
0x04    RESERVED
0x08    RESERVED
0x0C    CFCR      Flag clear register (write-only)
0x10    CSR       Status register (read-only)
0x14    CCR       Control register (EN, TCIE, priority, etc.)
0x28–   RESERVED
0x40    CTR1      Transfer config 1 (data width, increment)
0x44    CTR2      Transfer config 2 (REQSEL, DREQ, TCEM)
0x48    CBR1      Block count (BNDT field)
0x4C    CSAR      Source address
0x50    CDAR      Destination address
```

### Key Register Offsets for SPI_TypeDef
```
0x00    CR1       (SPE, CSTART)
0x04    CR2       (TSIZE[15:0], TSER[31:16])
0x08    CFG1      (RXDMAEN, TXDMAEN, baudrate, data size)
0x0C    CFG2      (CPOL, CPHA, etc.)
0x14    IER       (interrupt enables — should be 0)
0x18    IFCR      (flag clear, write-only)
0x20    TXDR      (transmit data)
0x30    RXDR      (receive data)
```

### Code Flow

#### `ads_dma_setup()` — One-time configuration
```c
dma_rx = GPDMA1_Channel1;
dma_tx = GPDMA1_Channel0;
memset(ads_tx_dma, 0, sizeof(ads_tx_dma));   // zero TX buffer

CLEAR_BIT(dma_rx->CCR, DMA_CCR_EN);          // disable both channels
CLEAR_BIT(dma_tx->CCR, DMA_CCR_EN);
dma_rx->CFCR = DMA_CFCR_ALL;                 // clear all flags
dma_tx->CFCR = DMA_CFCR_ALL;

dma_rx->CSAR = (uint32_t)&SPI1->RXDR;        // RX: peripheral → memory
dma_rx->CDAR = (uint32_t)ads_rx_dma;
dma_rx->CBR1 = ADS_FRAME_BYTES;              // 12 bytes
dma_tx->CSAR = (uint32_t)ads_tx_dma;         // TX: memory → peripheral
dma_tx->CDAR = (uint32_t)&SPI1->TXDR;
dma_tx->CBR1 = ADS_FRAME_BYTES;

SET_BIT(dma_rx->CCR, DMA_CCR_TCIE);          // enable RX TC interrupt only

CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);           // disable SPI
SPI1->IFCR = SPI_IFCR_ALL;                   // clear all SPI flags
MODIFY_REG(SPI1->CR2, SPI_CR2_TSIZE, ADS_FRAME_BYTES);  // set TSIZE=12
```

**Assumption:** CTR1, CTR2 (request line, data width, increment) are preserved from `HAL_DMA_Init()` which was called during `HAL_SPI_MspInit()`. These are NOT re-written.

#### `ads_fast_start()` — Per-transfer (called from EXTI2 ISR)
```c
GPIOA->BSRR = ADC_CS_Pin << 16;              // CS LOW (direct GPIO)

dma_rx->CFCR = DMA_CFCR_ALL;                 // clear RX flags
dma_rx->CBR1 = ADS_FRAME_BYTES;              // reload byte count
dma_tx->CFCR = DMA_CFCR_ALL;                 // clear TX flags
dma_tx->CBR1 = ADS_FRAME_BYTES;

SET_BIT(dma_rx->CCR, DMA_CCR_EN);            // enable RX channel
SET_BIT(dma_tx->CCR, DMA_CCR_EN);            // enable TX channel

SPI1->IFCR = SPI_IFCR_ALL;                   // clear SPI flags
MODIFY_REG(SPI1->CR2, SPI_CR2_TSIZE, 12);    // reload TSIZE
SET_BIT(SPI1->CFG1, RXDMAEN | TXDMAEN);      // enable SPI DMA
SET_BIT(SPI1->CR1, SPI_CR1_SPE);             // enable SPI
SET_BIT(SPI1->CR1, SPI_CR1_CSTART);          // start transfer
```

#### `ads_fast_complete()` — Called from GPDMA1_CH1 ISR on TC
```c
GPIOA->BSRR = ADC_CS_Pin;                    // CS HIGH

CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);           // disable SPI
CLEAR_BIT(SPI1->CFG1, RXDMAEN | TXDMAEN);    // disable SPI DMA
SPI1->IFCR = SPI_IFCR_ALL;                   // clear SPI flags

dma_rx->CFCR = DMA_CFCR_TCF;                 // clear RX TC flag
dma_tx->CFCR = DMA_CFCR_ALL;                 // clear all TX flags

// Extract 24-bit samples from rx buffer, update stats
ads_dma_busy = 0;
```

### ISR Wiring (stm32h5xx_it.c)

```c
void EXTI2_IRQHandler(void) {
    extern void ads_fast_drdy_handler(void);
    ads_fast_drdy_handler();
    return;                           // bypass HAL_GPIO_EXTI_IRQHandler
}

void GPDMA1_Channel1_IRQHandler(void) {
    extern int ads_fast_dma_complete_handler(void);
    if (ads_fast_dma_complete_handler()) return;  // bypass HAL if handled
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);  // fallback to HAL
}

// GPDMA1_Channel0_IRQHandler: NO fast-path intercept (TX has no TCIE)
```

### Race Protection

```c
static volatile uint8_t  ads_dma_stop = 1;  // prevents DRDY ISR before setup
static volatile uint8_t  ads_dma_busy;      // prevents re-entrant DMA start

void ads_fast_drdy_handler(void) {
    EXTI->FPR1 = ADC_DRDY_Pin;     // clear EXTI2 pending
    ads_stats.drdy_count++;
    if (ads_dma_stop) return;       // early exit before DMA is ready
    if (ads_dma_busy) { miss_count++; return; }
    ads_dma_busy = 1;
    ads_fast_start();
}
```

---

## 6. First Test Run (HAL DMA, Before Optimization)

This was the WORKING baseline using `HAL_SPI_TransmitReceive_DMA()`:

```
Total DRDY=5098295 DMA=1287451 miss=3797964
```

**Result:** ~21,333 DMA transfers/sec (33% of 64 kSPS). HAL overhead caused 75% miss rate.

Per-second live diagnostics:
```
ADC: DRDY_SW=85388 DRDY_HW=342 DMA=22623 miss=67869
```

**Issues identified:**
1. **DMA miss rate:** HAL_SPI_TransmitReceive_DMA overhead ~47 µs >> 15.625 µs period
2. **DRDY_SW = 85K/s:** Re-triggering from level-mode DRDY (each SPI CS transaction re-asserts DRDY)
3. **DRDY_HW = 342/s:** TIM3 prescaler bug (PSC=249 shadow register not reloaded)

---

## 7. Fixes Applied

### Fix 1: TIM3 Prescaler Reload
```c
__HAL_TIM_SET_PRESCALER(&htim3, 0);
htim3.Instance->EGR = TIM_EGR_UG;              // force update event
__HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);  // clear spurious UIF
```

### Fix 2: Register-Level DMA (bypass HAL)
See Section 5 above. All GPDMA register writes done directly.

### Fix 3: `ads_dma_stop = 1` Global Init
Prevents HardFault from DRDY interrupts firing before `ads131m02_start_continuous()`.

### Fix 4: TSIZE Reload + SPI Flag Clear
SPI TSIZE auto-decrements to 0 after transfer. Must reload before each new transfer.
Also clear all SPI flags (IFCR) before re-enabling.

### Fix 5: HAL State Fixup in `stop_continuous()`
```c
hspi1.State = HAL_SPI_STATE_READY;
handle_GPDMA1_Channel0.State = HAL_DMA_STATE_READY;
handle_GPDMA1_Channel1.State = HAL_DMA_STATE_READY;
```
Required for subsequent blocking `HAL_SPI_TransmitReceive()` calls in polarity test.

### Fix 6: Timeout on `stop_continuous()` busy-wait
```c
uint32_t t0 = HAL_GetTick();
while (ads_dma_busy) {
    if (HAL_GetTick() - t0 > 50) { printf("WARN: timeout"); break; }
    __NOP();
}
```

---

## 8. Symptom: System Hangs After Register-Level DMA Enable

### Test Run 2 (partial output visible)
```
DIAG: TIM3 DRDY edge counter started (TI1FP1/PB4)
==
```
System printed "==" (beginning of polarity test banner) then halted.

### Test Run 3 (nothing after TIM3 init)
```
DIAG: TIM3 DRDY edge counter started (TI1FP1/PB4)

```
No further output. Not even the `[MAIN]` breadcrumb before `start_continuous()`.

**The failure is 100% reproducible.**

---

## 9. Possible Root Causes (Under Investigation)

### Hypothesis A: DMA Transfer Never Completes (Deadlock)
If the DMA transfer doesn't complete (no TCF), `ads_dma_busy` stays 1.
When `polarity_test()` → `stop_continuous()` → `while(ads_dma_busy)` → infinite loop.
**Now mitigated** with timeout (Fix 6). Diagnostic build will expose this.

### Hypothesis B: GPDMA CTR1/CTR2 Corrupted
Our code writes CSAR/CDAR/CBR1/CFCR/CCR but relies on CTR1/CTR2 being preserved from `HAL_DMA_Init()`.
If `HAL_SPI_Init()` (called during LTC6903 mode switch) somehow resets DMA channels, CTR1/CTR2 could be zeroed.
**But:** `HAL_SPI_MspInit()` is only called on first init (state==RESET). Subsequent `HAL_SPI_Init()` skips MspInit.
**Diagnostic build** dumps CTR1/CTR2 to verify request lines (REQSEL 6=RX, 7=TX).

### Hypothesis C: SPI IER Non-Zero
If SPI1 IER has stale interrupt enables, an SPI interrupt fires during DMA transfer.
`HAL_SPI_IRQHandler` with unexpected state could cause issues.
**Diagnostic build** prints IER value.

### Hypothesis D: GPDMA Channel 0 (TX) Spurious Interrupt
TX channel has no TCIE set, but NVIC is enabled. If any error flag triggers an interrupt,
`HAL_DMA_IRQHandler` runs for Channel 0 with HAL state = READY. Could cause confusion.
Not intercepted by fast path.

### Hypothesis E: CFG1 Write While SPE=1
`SET_BIT(SPI1->CFG1, RXDMAEN | TXDMAEN)` requires SPE=0 per RM0481.
In `ads_fast_start()`, SPE is cleared before this call (from previous `ads_fast_complete()`).
But for the VERY FIRST transfer after `ads_dma_setup()`, SPE is also 0. Should be fine.

### Hypothesis F: HardFault from printf During ISR Context
If the hang occurs during `printf` in main while DRDY ISRs are firing at 64 kHz,
and `printf` itself is interrupted, there could be re-entrancy in the UART/USB driver.
**But:** The DRDY ISR handler doesn't call printf.

### Hypothesis G: SPI Peripheral Left Enabled (SPE=1)
After `ltc6903_init()` restores Mode 1:
```c
__HAL_SPI_ENABLE(&hspi1);   // SPE = 1
```
Then `ads131m02_init()` uses `HAL_SPI_TransmitReceive()` (polling). The HAL sets SPE if needed
and `SPI_CloseTransfer()` clears it at the end. So SPE should be 0 when we reach `ads_dma_setup()`.
**Diagnostic build** prints CR1 to verify.

---

## 10. Diagnostic Build (Current Code)

The current build adds:

1. **Register dumps** before and after the first DMA transfer:
   - DMA RX: CCR, CTR1, CTR2, CBR1, CSAR, CDAR, CSR
   - DMA TX: CCR, CTR1, CTR2, CBR1, CSAR, CDAR, CSR
   - SPI1: CR1, CR2, CFG1, CFG2, SR, IER

2. **Manual single-transfer test** (polled, no DRDY interrupt):
   - Calls `ads_fast_start()` with interrupts briefly disabled
   - Polls for `DMA_CSR_TCF` on RX channel with 100ms timeout
   - Calls `ads_fast_complete()` on success
   - Prints raw RX bytes and decoded CH0/CH1

3. **Breadcrumb printf** statements at each stage:
   ```
   [MAIN] calling ads131m02_start_continuous()...
   [ADC] start_continuous: ads_dma_setup...
   [ADC] start_continuous: setup done
   [DMA-post-setup] RX CCR=... CTR1=... CTR2=... CBR1=... CSAR=... CDAR=... CSR=...
   [DMA-post-setup] TX CCR=... CTR1=... CTR2=... CBR1=... CSAR=... CDAR=... CSR=...
   [SPI-post-setup] CR1=... CR2=... CFG1=... CFG2=... SR=... IER=...
   [DMA-TEST] starting single manual transfer...
   [DMA-TEST] OK  CH0=+xxx CH1=+xxx  RX: xx xx xx xx xx xx xx xx xx xx xx xx
        OR
   [DMA-TEST] TIMEOUT waiting for RX TCF! CSR_RX=... CSR_TX=... SPI_SR=...
   [DMA-post-test] ...
   [ADC] enabling DRDY-triggered DMA...
   [MAIN] start_continuous returned, waiting 500 ms...
   [MAIN] 500 ms survived, calling polarity_test...
   ```

4. **Timeout on `stop_continuous()`** busy-wait (50 ms):
   ```
   [ADC] WARN: stop_continuous busy-wait timeout, forcing
   ```

### Expected Diagnostic Outcomes

| Output Stops At | Diagnosis |
|-----------------|-----------|
| Before `[MAIN] calling...` | Crash in `diag_drdy_init()` or earlier |
| After `ads_dma_setup...` but before `setup done` | Crash in DMA register writes |
| After `[DMA-post-setup]` dump | Register dump reveals misconfiguration |
| `[DMA-TEST] TIMEOUT` | DMA transfer doesn't complete — CTR2/REQSEL or SPI issue |
| `[DMA-TEST] OK` then no `[ADC] enabling...` | Crash in second register dump |
| `[MAIN] start_continuous returned` then silence | Crash under DRDY-triggered DMA load |
| `500 ms survived` then silence | Crash in `polarity_test` → `stop_continuous` |
| `WARN: busy-wait timeout` | DMA transfer hangs, `ads_dma_busy` stuck at 1 |

### Key Register Values to Check in Output

**DMA RX CTR2 (Channel 1):**
- Bits [6:0] = REQSEL → must be 6 (SPI1_RX)
- Bit 17 = DREQ → must be 0 (periph-to-memory)

**DMA TX CTR2 (Channel 0):**
- Bits [6:0] = REQSEL → must be 7 (SPI1_TX)
- Bit 17 = DREQ → must be 1 (memory-to-periph)

**DMA RX CTR1:**
- Bit 3 = SINC → 0 (source fixed = RXDR)
- Bit 15 = DINC → 1 (destination increment = memory buffer)

**DMA TX CTR1:**
- Bit 3 = SINC → 1 (source increment = memory buffer)
- Bit 15 = DINC → 0 (destination fixed = TXDR)

**SPI1 IER:**
- Must be 0x00000000 (no SPI interrupts enabled)

**SPI1 CR1:**
- SPE (bit 0) → must be 0 after setup
- CSTART (bit 9) → must be 0 after setup

**SPI1 CR2:**
- TSIZE[15:0] → must be 12 (0x0000000C)

---

## 11. Source File Locations

| File | Purpose |
|------|---------|
| `Core/Src/adc_ads131m02.c` | Full DMA driver + polarity test + DRDY_FMT A/B test |
| `Core/Inc/adc_ads131m02.h` | Register defs, macros, stats struct, public API |
| `Core/Src/stm32h5xx_it.c` | ISR wiring (EXTI2, GPDMA1_CH0, GPDMA1_CH1) |
| `Core/Src/spi.c` | SPI1 init + DMA channel init (HAL_SPI_MspInit) |
| `Core/Src/gpdma.c` | GPDMA clock enable + NVIC setup |
| `Core/Src/main.c` | Boot sequence, Phase 7 entry point |
| `Core/Src/diag_timers.c` | TIM8 CLKIN + TIM3 DRDY hardware counters |
| `Core/Src/osc_ltc6903.c` | LTC6903 oscillator driver (SPI mode switching) |

---

## 12. Alternative Approaches If Register-Level DMA Fails

### Option A: HAL DMA with Reduced Overhead
Use `HAL_SPI_TransmitReceive_DMA()` but:
- Skip the HAL lock/unlock (`__HAL_LOCK`/`__HAL_UNLOCK`)
- Pre-register callbacks to avoid dispatch overhead
- Accept ~33% capture rate initially, optimize later

### Option B: Circular DMA with Fixed-Size Transfers
Configure DMA in circular mode. SPI generates continuous clocks.
ADS131M02 gates data via DRDY. Requires careful TSIZE management.

### Option C: DMA Linked-List Mode
Use GPDMA linked-list to auto-reload transfer parameters.
One LLI node per frame, circular chain. EXTI2 only gates CS.
Most complex but potentially fastest.

### Option D: Increase SYSCLK to 250 MHz
Reduces HAL overhead proportionally. At 250 MHz, HAL DMA might fit
in the 15.625 µs window. Requires PLL reconfiguration and
recalculating SPI/SDMMC prescalers.

### Option E: Use DMA DREQ Without EXTI
Let SPI run continuously, triggered by DMA request from SPI peripheral.
CS managed differently (hardware NSS or always-low).
Requires SPI to only clock when data is available.

---

## 13. Reference Documents

- **RM0481** — STM32H5 Reference Manual (GPDMA, SPI, EXTI sections)
- **Datasheets/ads131m02.pdf** (SBAS853A) — ADS131M02 datasheet
- **References/ADS131M02_CONTEXT.md** — Derived register/timing summary
- **STM32H5 HAL Source:**
  - `Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma.c`
  - `Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_spi.c`
- **CMSIS Device Header:**
  - `Drivers/CMSIS/Device/ST/STM32H5xx/Include/stm32h562xx.h`

---

## 14. Conversation History

The full development history for Phase 7 is in the Cursor agent transcript:
- [Phase 7 DMA Development](60dafc08-4f85-44cd-bdbe-c106058fe908)

Previous plans:
- `.cursor/plans/fix_phase_7_dma_c8ed3b1c.plan.md` — DMA fix plan
- `.cursor/plans/phase_7_ads131m02_dma_6222cc87.plan.md` — Original Phase 7 plan

---

## 15. Diagnostic Build Results (2026-04-12, Test Run 4)

### Raw Output (Repeating Pattern)
```
DIAG: TIM3 DRDY edge counter started (TI1FP1/PB4)
[MAIN] calling ads131m02_start_continuous()...
[ADC] start_continuous: ads_dma_setup...
[ADC] start_continuous: setup done
[DMA-post-setup] RX CCR=0x00800100 CTR1=0x00080000 CTR2=0x00000006 CBR1=12 CSAR=0x40013030 CDAR=0x20000800 CSR=0x00000001
[DMA-post-setup] TX CCR=0x00800000 CTR1=0x00000008 CTR2=0x00000407 CBR1=12 CSAR=0x200007F4 CDAR=0x40013020 CSR=0x00000001
[SPI-post-setup] CR1=0x00001000 CR2=0x0000000C CFG1=0x10070007 CFG2=0x05400000 SR=0x00019007 IER=0x00000000
[DMA-TEST] starting single manual transfer...
[DMA-TEST] TIMEOUT waiting for RX TCF! CSR_RX=0x00000201 CSR_TX=0x00000001 SPI_SR=0x00019007
[ADC] MANUAL DMA TEST FAILED — aborting continuous
```
**This pattern repeated identically for every call to `ads131m02_start_continuous()` — the failure is deterministic.**

### Complete Register Decode

#### DMA RX (Channel 1) — All correct
| Register | Value        | Decode |
|----------|-------------|--------|
| CCR      | 0x00800100  | PRIO=low-high-weight (bit 23), TCIE=1 (bit 8), EN=0 ✓ |
| CTR1     | 0x00080000  | DINC=1 (bit 19, dest increment), SINC=0 (src fixed = RXDR) ✓ |
| CTR2     | 0x00000006  | REQSEL=6 (SPI1_RX), DREQ=0 (periph-to-memory) ✓ |
| CBR1     | 12          | 12 bytes ✓ |
| CSAR     | 0x40013030  | &SPI1->RXDR (SPI1 base 0x40013000 + RXDR offset 0x30) ✓ |
| CDAR     | 0x20000800  | ads_rx_dma buffer in SRAM ✓ |
| CSR      | 0x00000001  | IDLEF=1 (channel idle) ✓ |

#### DMA TX (Channel 0) — All correct
| Register | Value        | Decode |
|----------|-------------|--------|
| CCR      | 0x00800000  | PRIO=low-high-weight (bit 23), TCIE=0, EN=0 ✓ |
| CTR1     | 0x00000008  | SINC=1 (bit 3, src increment), DINC=0 (dest fixed = TXDR) ✓ |
| CTR2     | 0x00000407  | REQSEL=7 (SPI1_TX), DREQ=1 (bit 10, mem-to-periph) ✓ |
| CBR1     | 12          | 12 bytes ✓ |
| CSAR     | 0x200007F4  | ads_tx_dma buffer in SRAM ✓ |
| CDAR     | 0x40013020  | &SPI1->TXDR (SPI1 base 0x40013000 + TXDR offset 0x20) ✓ |
| CSR      | 0x00000001  | IDLEF=1 (channel idle) ✓ |

**Conclusion:** DMA channel configuration is 100% correct. Request lines, data widths, increment settings, and addresses all match expected values.

#### SPI1 — THE BUG IS HERE
| Register | Value        | Decode |
|----------|-------------|--------|
| CR1      | 0x00001000  | SSI=1 (bit 12, internal slave select for soft NSS), SPE=0, CSTART=0 ✓ |
| CR2      | 0x0000000C  | TSIZE=12 ✓ |
| CFG1     | 0x10070007  | MBR=001 (/4 prescaler), DSIZE=7 (8-bit), FTHLV=0 (1 data), RXDMAEN=0, TXDMAEN=0 ✓ |
| CFG2     | 0x05400000  | CPOL=0, CPHA=1 (Mode 1) ✓ |
| **SR**   | **0x00019007** | **RXP=1, TXP=1, DXP=1, TXC=1 (bit 12), RXWNE=1 (bit 15), CTSIZE!=0** |
| IER      | 0x00000000  | No SPI interrupts ✓ |

**SPI SR = 0x00019007 — STALE DATA IN RX FIFO!**

- **RXP (bit 0) = 1:** Receive packet available — data sitting in RX FIFO
- **RXWNE (bit 15) = 1:** At least one full 32-bit word in RX FIFO
- **FIFOL in RX CSR after timeout = 2:** RX DMA read 2 bytes from the stale FIFO, then stalled

### Root Cause Analysis

**The SPI RX FIFO contains stale data from previous `HAL_SPI_TransmitReceive()` calls in `ads131m02_init()`.**

Chain of events:
1. `ads131m02_init()` performs many blocking SPI transfers (register reads/writes)
2. `HAL_SPI_TransmitReceive()` → `SPI_CloseTransfer()` → `__HAL_SPI_DISABLE()` sets SPE=0
3. Per RM0481, SPE 1→0 transition flushes FIFOs
4. But the last transfer may leave SPI in a state where residual data accumulates
5. By the time `ads_dma_setup()` runs, `SPI_SR.RXP=1` and `SPI_SR.RXWNE=1`
6. `ads_dma_setup()` does `CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE)` — but SPE is already 0!
7. **No 1→0 transition occurs → no FIFO flush!**

When `ads_fast_start()` enables SPE:
1. SPE goes 0→1, RXDMAEN is set, stale RXP=1 fires an immediate RX DMA request
2. RX DMA reads 2 stale bytes (FIFOL=2 in post-timeout CSR)
3. TSIZE counter is confused by the premature FIFO drain
4. TX DMA never receives a proper request (the SPI state machine is out of sync)
5. Transfer stalls permanently → `ads_dma_busy` stays 1 → polarity test's `stop_continuous()` busy-waits

### Fix Applied

In `ads_dma_setup()`, **force-flush SPI FIFOs** by toggling SPE 1→0:

```c
CLEAR_BIT(SPI1->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
SET_BIT(SPI1->CR1, SPI_CR1_SPE);      // enable (even if already off)
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);    // disable → 1→0 transition flushes FIFOs
SPI1->IFCR = SPI_IFCR_ALL;            // clear all flags
```

This ensures RXP=0, RXWNE=0, TXP=1 (empty TX FIFO default) regardless of prior SPI state.

### Key Insight for Future

On STM32H5 SPI: **always force a SPE 1→0 transition before re-purposing SPI for DMA if any prior blocking/polling transfers were performed.** A `CLEAR_BIT(SPE)` when SPE is already 0 does NOT flush FIFOs.

---

## 16. Test Run 5 — SPE Flush Fix Applied (2026-04-12)

### What Changed
Added SPE 1→0 toggle in `ads_dma_setup()` to flush stale SPI FIFO data.

### Results Summary

**The SPE flush fix worked — SPI SR is now clean after setup.**
- Post-setup `SR=0x000C1002` (was 0x00019007). Decoded:
  - RXP=0 (bit 0 clear) — **no more stale RX data** ✓
  - TXP=1 (bit 1) — TX FIFO empty/ready ✓
  - TXC=1 (bit 12) — TX complete ✓
  - RXWNE=0 (bit 15 clear) — **RX FIFO empty** ✓
  - Bits [19:18] = 0x3 → CTSIZE residual (don't-care when SPE=0)

**However, two new issues emerged:**

### Issue A: First DMA Transfer Fails, Second Succeeds

| Call # | Context | Manual DMA Test Result |
|--------|---------|----------------------|
| 1st | `main.c` initial `start_continuous()` | TIMEOUT (CSR_RX=0x201, CSR_TX=0x001) |
| 2nd | `polarity_test()` → `start_continuous()` iteration 1 | TIMEOUT (same pattern) |
| 3rd | `polarity_test()` → `start_continuous()` iteration 2 | **SUCCESS!** CH0=-1077882 CH1=+1081614 |

The DMA transfer works on the 3rd call but not the 1st or 2nd. The register dumps are
identical across all three calls (same CCR, CTR1, CTR2, CBR1, CSAR, CDAR, SR values).
This suggests a **timing-dependent issue** — something that changes between calls but
isn't captured in the register dump.

**Possible explanations for first-call failure:**
1. The ADS131M02 may not be ready to send data on the very first SPI transfer after
   `ads131m02_stop_continuous()` + `ads131m02_write_reg()` (polarity test changes MUX).
   The 2nd call also writes MUX registers. By the 3rd call, the ADC has had time to settle.
2. The `ads131m02_stop_continuous()` in `polarity_test()` does a full cleanup
   (disable SPI, DMA, clear flags, reset HAL states). This more thorough cleanup may
   leave the SPI in a better state than the initial call from `main.c`.
3. There may be a GPDMA channel state that isn't visible in the register dump but
   differs between first-use-after-HAL-init and subsequent uses.

**Successful transfer decode (3rd call):**
```
RX: 01 03 00 EF 8D 86 10 81 0E 06 10 00
```
- Word 0 (STATUS): 0x0103 — DRDY0+DRDY1 set, normal operation ✓
- Word 1 (CH0): 0xEF8D86 → sign-extend → -1077882 (negative DC test signal) ✓
- Word 2 (CH1): 0x10810E → sign-extend → +1081614 (positive DC test signal) ✓
- Word 3 (CRC): 0x061000 (CRC disabled, don't-care)

**This confirms the register-level DMA is fundamentally working.**

### Issue B: DRDY-Triggered Path Hangs After Successful Manual Test

After the successful 3rd manual test, the code printed:
```
[ADC] enabling DRDY-triggered DMA...
```
Then output stopped. No `[MAIN]` breadcrumbs, no `[ADC] WARN: busy-wait timeout`.

This means the system hung INSIDE the 1-second wait loop of `polarity_test()`:
```c
while (HAL_GetTick() - t0 < 1000) {
    ux_system_tasks_run();
    cdc_poll();
}
```

The `stop_continuous()` timeout (50 ms) was NOT triggered, which means `polarity_test`
never even reached `stop_continuous()`. **The system crashed (HardFault) rather than
deadlocking in a busy-wait.**

**Possible causes of DRDY-triggered crash:**
1. The `ads_fast_dma_complete_handler()` ISR fires while `ads_fast_start()` hasn't
   fully returned — but EXTI2 and GPDMA1_CH1 are same priority (0), so no preemption.
2. GPDMA1_Channel0 (TX) fires an unexpected interrupt. The TX channel has no TCIE,
   but an error flag could trigger an interrupt if the NVIC is enabled and any error
   IE bit leaked in. TX CCR = 0x00800000 shows no interrupt enables, so this is unlikely.
3. The SPI1 interrupt (NVIC enabled, priority 0) fires during the DMA transfer.
   IER=0x00000000, so no SPI interrupts should fire. But if the DMA transfer causes
   an SPI error (OVR, UDR), and IER somehow gets set... unlikely with IER=0.
4. `HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0)` is called (TX channel NVIC fires)
   with HAL state = READY. The HAL handler might read CSR, see flags from our
   register-level transfer, and take unexpected action.

### Next Steps (When Resuming)

1. **Fix Issue A (first-call failure):** Try adding a small delay or dummy SPI frame
   before the first manual DMA test. Or try calling `ads131m02_stop_continuous()`
   before the first `start_continuous()` to ensure full cleanup.

2. **Fix Issue B (DRDY-triggered crash):** The most likely cause is the GPDMA1_CH0
   (TX) NVIC interrupt. When the TX DMA completes, TCF is set in CSR. Even though
   no TCIE is enabled, if ANY interrupt flag is pending in the NVIC from a prior
   HAL operation, the `GPDMA1_Channel0_IRQHandler` fires and `HAL_DMA_IRQHandler`
   processes it with potentially stale HAL state. **Consider adding a fast-path
   intercept for GPDMA1_Channel0_IRQHandler** similar to Channel1, or **disable
   the GPDMA1_Channel0 NVIC** entirely since TX completion doesn't need an interrupt.

3. **Alternative for Issue B:** Disable GPDMA1_Channel0 NVIC in `ads_dma_setup()`:
   ```c
   HAL_NVIC_DisableIRQ(GPDMA1_Channel0_IRQn);
   ```
   The TX DMA completion doesn't need an interrupt — we only care about RX completion.

4. **Keep diagnostic prints** until both issues are resolved, then strip them for
   production.

---

## 17. Fixes Applied for Issues A & B (2026-04-12, session 2)

### Issue B Fix: Prevent HAL from processing DMA interrupts during register-level transfers

**Root cause:** When our register-level TX DMA completes, `GPDMA1_Channel0_IRQHandler`
fires and `HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0)` processes the flags with stale
HAL state, corrupting the SPI/DMA pipeline. Similarly, any non-TCF flag on Channel 1
could fall through to HAL.

**Changes:**

1. **`stm32h5xx_it.c` — `GPDMA1_Channel0_IRQHandler`:** Intercepts all interrupts,
   clears all DMA flags (`TCF|HTF|DTEF|ULEF|USEF|SUSPF|TOF`), and returns immediately.
   HAL `DMA_IRQHandler` is never reached.

2. **`stm32h5xx_it.c` — `GPDMA1_Channel1_IRQHandler`:** Changed from conditional
   early return to unconditional return. If `ads_fast_dma_complete_handler()` doesn't
   handle the interrupt (TCF not set), we clear all flags and return. HAL never runs.

3. **`adc_ads131m02.c` — `ads_dma_setup()`:** Added at end:
   ```c
   SPI1->IER = 0;                                 // no SPI interrupts
   HAL_NVIC_DisableIRQ(GPDMA1_Channel0_IRQn);     // TX DMA NVIC off
   NVIC_ClearPendingIRQ(GPDMA1_Channel0_IRQn);    // clear stale pending
   NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn);
   NVIC_ClearPendingIRQ(SPI1_IRQn);
   ```

4. **`adc_ads131m02.c` — `ads_fast_complete()`:** Changed `dma_rx->CFCR` from
   `DMA_CFCR_TCF` to `DMA_CFCR_ALL` — clear all RX DMA flags, not just TCF.

### Issue A Fix: Retry logic + thorough cleanup on timeout

**Root cause:** First 1-2 DMA transfers timeout (TX DMA doesn't respond to requests),
then the 3rd succeeds. Likely residual internal DMA state from HAL initialization.
The timeout cleanup path was incomplete (didn't clear SPI DMA enables or flags),
leaving dirty state for the next attempt.

**Changes:**

5. **`adc_ads131m02.c` — `ads_manual_dma_test()` timeout path:** Now performs full
   cleanup: disables SPI DMA enables (`RXDMAEN|TXDMAEN`), clears `SPI_IFCR_ALL`,
   clears `DMA_CFCR_ALL` on both channels.

6. **`adc_ads131m02.c` — `ads131m02_start_continuous()`:** Wrapped in retry loop
   (up to 3 attempts). Each attempt calls `ads_dma_setup()` + `ads_manual_dma_test()`.
   Register dumps printed only on first attempt. On success, enables DRDY-triggered
   path; after 3 failures, aborts.

### Expected outcome

- **Issue B:** DRDY-triggered DMA should run without hanging, because GPDMA1_Channel0
  (TX) no longer triggers HAL processing. Pending NVIC flags from prior operations
  are cleared before enabling the DRDY path.
- **Issue A:** First call may still timeout, but retry logic ensures the manual test
  eventually succeeds (it worked on the 3rd call last time). Full cleanup between
  retries ensures each attempt starts from a clean state.
- **Steady state:** After successful start, the 64 kSPS DRDY-triggered DMA cycle
  should run with near-zero misses (register-level overhead ~3 µs << 15.625 µs period).

---

## 18. Test Run 6 — Analysis + Root Cause of "Issue A" (2026-04-12, session 2)

### Observations

Retry logic confirmed the DMA transfer eventually succeeds (attempt 2 or 3 of 3).
Register config is identical across all attempts. SPI SR is clean after setup
(`0x000C1002`). But after timeout: `CSR_RX=0x00000001 CSR_TX=0x00000001 SPI_SR=0x00019007`.

Key decode of timeout state:
- **CSR_RX=0x00000001**: IDLEF only. NO TCF. Channel is idle.
- **CSR_TX=0x00000001**: Same — idle, no TCF.
- **SPI_SR=0x00019007**: RXP=1, TXP=1, DXP=1, **TXC=1**, RXWNE=1.

**TXC=1 means the SPI transfer completed.** Data is sitting in the RX FIFO (RXP=1,
RXWNE=1). But both DMA channels show no TCF. The DMA transfers DID complete (channels
are idle, EN auto-cleared), but TCF was already consumed.

### Root Cause: ISR/Polling Race Condition

`ads_manual_dma_test()` polls `GPDMA1_Channel1->CSR & DMA_CSR_TCF` in a loop. But
**GPDMA1_Channel1_IRQn is enabled**, so the ISR fires when TCF is set:

1. `ads_fast_start()` fires the SPI DMA transfer (~5 µs at 18.75 MHz)
2. `__enable_irq()` — pending IRQs can now fire
3. RX DMA completes → TCF set → GPDMA1_Channel1_IRQn fires immediately
4. ISR: `ads_fast_dma_complete_handler()` sees TCF → calls `ads_fast_complete()`
5. `ads_fast_complete()` raises CS, disables SPE, **clears all DMA flags**
6. Return to polling loop: `CSR & TCF` → 0 (already cleared!) → loop continues
7. 100 ms timeout → reports TIMEOUT

The transfer succeeded every time, but the ISR consumed TCF before the polling loop.

The apparent "intermittent success on 3rd attempt" was a timing race: occasionally
the polling loop ran fast enough to see TCF before the ISR. The hardened IRQ handlers
(unconditional return) made the race even less likely to win.

### Additional Issues Identified

**Unsafe SPI shutdown timing:** `ads_fast_complete()` disabled SPE immediately on
RX DMA TC without waiting for SPI_SR_TXC. On STM32H5, disabling SPI before the
last clock edge completes can leave internal state dirty.

**SysTick starvation:** At 64 kSPS, EXTI2 + GPDMA1_CH1 fire ~128K IRQs/sec at
priority 0. SPI1_IRQn (priority 1) was still enabled, adding potential overhead.
HAL_GetTick()-based timeouts in the manual test could stall if TIM6 was delayed.

**No frame discard:** First frame after MUX/polarity change or mode switch may
contain transition data. Not discarded.

---

## 19. Fixes Applied — Session 2, Batch 2 (2026-04-12)

### Fix 1: Eliminate ISR/polling race in `ads_manual_dma_test()`

```c
HAL_NVIC_DisableIRQ(GPDMA1_Channel1_IRQn);
NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn);
// ... run transfer, poll TCF directly ...
HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);
```

With Channel1 NVIC disabled, the polling loop owns TCF exclusively. Re-enabled
after the test completes (success or timeout).

### Fix 2: DWT->CYCCNT timeout instead of HAL_GetTick()

```c
uint32_t cyc0 = DWT->CYCCNT;
uint32_t timeout_cyc = SystemCoreClock / 10;  /* 100 ms */
while (!(... & DMA_CSR_TCF))
    if ((DWT->CYCCNT - cyc0) > timeout_cyc) break;
```

DWT cycle counter is immune to SysTick starvation. Initialized in `ads_dma_setup()`:
```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
```

### Fix 3: Wait for TXC before disabling SPE

In `ads_fast_complete()` and the skip-frame handler:
```c
while (!(SPI1->SR & SPI_SR_TXC)) __NOP();
```
Ensures the SPI has fully completed the last clock cycle before SPE→0.
Also reordered: RXDMAEN/TXDMAEN disabled BEFORE SPE (safer per RM0481).

### Fix 4: Disable SPI1_IRQn

Added `HAL_NVIC_DisableIRQ(SPI1_IRQn)` in `ads_dma_setup()`. With `IER=0` already,
SPI interrupts shouldn't fire, but disabling the NVIC eliminates any residual risk.
Only EXTI2 and GPDMA1_CH1 remain active during continuous capture.

### Fix 5: IDLEF wait after disabling DMA channels

```c
CLEAR_BIT(dma_rx->CCR, DMA_CCR_EN);
CLEAR_BIT(dma_tx->CCR, DMA_CCR_EN);
while (!(dma_rx->CSR & DMA_CSR_IDLEF)) __NOP();
while (!(dma_tx->CSR & DMA_CSR_IDLEF)) __NOP();
```

Per RM0481: "After clearing EN, the application must wait until IDLEF=1 before
reprogramming CxSAR, CxDAR, CxTR1, CxTR2, CxBR1." Previously violated.

### Fix 6: Skip first 2 frames after DRDY-triggered DMA enable

```c
ads_skip_frames = 2;
ads_dma_stop = 0;
```

In `ads_fast_dma_complete_handler()`, if `ads_skip_frames > 0`, the frame is
discarded (SPI properly shut down, flags cleared, busy released) without
updating stats. Handles transition/settling frames after MUX changes.

### Expected Outcome

- **Manual DMA test:** Should succeed on **first attempt** every time (no race).
- **DRDY-triggered path:** Should run without hanging (no SPI1/CH0 HAL interference,
  safe SPE shutdown, proper TXC sequencing).
- **Steady state:** ~64K DMA/sec, near-zero misses, valid ADC data.

---

## 20. Test Run 7 — Manual Test Succeeds, DRDY Mode Stalls (2026-04-12, session 2)

### Results

Manual DMA test now succeeds on **first attempt** on every boot. The ISR/polling
race fix (CH1 NVIC disable during test) was confirmed correct.

**New failure point:** After `[ADC] enabling DRDY-triggered DMA...`, partial foreground
print `[MAIN] s` then output stops. Not a HardFault (foreground code ran briefly).

### Root Cause: CFG1 Written While SPE=1 (RM0481 Violation)

Post-test register dump revealed:
```
[SPI-post-test] CFG1=0x1007C007   (bits 14-15 SET = RXDMAEN + TXDMAEN)
[SPI-post-test] SR=0x00019007     (RXP=1, RXWNE=1 = stale RX data)
```

**But `ads_fast_complete()` explicitly clears RXDMAEN/TXDMAEN!** The write was
silently ignored because SPE was still 1 when CFG1 was modified.

Previous fix (session 2 batch 2) reordered the shutdown to:
```c
CLEAR_BIT(SPI1->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);  // SPE=1 here!
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
```

Per RM0481: "Configuration registers (CFG1, CFG2) can only be written when SPE=0."
The CFG1 write was a no-op. RXDMAEN/TXDMAEN remained set.

**Consequence:** When the first DRDY-triggered `ads_fast_start()` ran:
1. SPI had stale RX FIFO data (RXP=1, RXWNE=1)
2. RXDMAEN was already set from the manual test
3. Setting SPE=1 immediately triggered RX DMA on stale data
4. Frame alignment corrupted → likely infinite miss/retry cascade

### Fixes Applied — Session 2, Batch 3

**Fix 1: Restore correct SPE→CFG1 ordering in `ads_fast_complete()`:**
```c
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);     // SPE=0 first → flushes FIFOs
CLEAR_BIT(SPI1->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);  // now safe
```

Same fix applied to the skip-frame handler in `ads_fast_dma_complete_handler()`.

**Fix 2: Hard cleanup between manual test and DRDY enable:**
```c
HAL_NVIC_DisableIRQ(EXTI2_IRQn);      // gate DRDY interrupts

// Force-clean SPI state
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
CLEAR_BIT(SPI1->CFG1, RXDMAEN|TXDMAEN);
SET_BIT(SPI1->CR1, SPI_CR1_SPE);      // SPE 1→0 flush
CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
SPI1->IFCR = SPI_IFCR_ALL;

// Force-clean DMA state
disable channels, wait IDLEF, clear flags, reload CBR1, reload TSIZE

// Clear all pending edge/IRQ state
__HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_Pin);
NVIC_ClearPendingIRQ(EXTI2_IRQn);
NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn);

// Verify (first-attempt only)
ads_dump_dma_regs("pre-drdy");

// Enable DRDY path atomically
ads_skip_frames = 2;
ads_dma_busy = 0;    // before stop, so handler sees ready state
ads_dma_stop = 0;
HAL_NVIC_EnableIRQ(EXTI2_IRQn);
```

**Fix 3: Simplified main.c prints:**
Replaced `HAL_Delay(500)` + multiple printf calls with a cooperative wait loop
(`ux_system_tasks_run()` + `cdc_poll()`) followed by a single stats summary print.
This eliminates blocking UART calls while 128K IRQ/sec is active.

### Expected Outcome

- Post-test CFG1 should now show `0x10070007` (RXDMAEN/TXDMAEN clear)
- Post-test SR should show `0x000C1002` (clean, no stale RX data)
- Pre-DRDY dump should confirm fully clean state before live mode
- `[MAIN] 500 ms:` line should print with DMA/DRDY counters > 0
- Polarity test should run to completion

---

## 21. Test Run 8 — Clean SPI State, CSAR/CDAR Bug Found (2026-04-12)

### Results

CFG1/SPE ordering fix confirmed: post-test CFG1=`0x10070007` (clean).
Pre-DRDY SPI state fully clean: SR=`0x000C1002`, CFG1=`0x10070007`.

**But pre-DRDY DMA addresses are WRONG:**
```
[DMA-pre-drdy] RX CDAR=0x2000080C   (should be 0x20000800)
[DMA-pre-drdy] TX CSAR=0x20000800   (should be 0x200007F4)
```

### Root Cause: GPDMA Updates CSAR/CDAR After Transfer

On STM32H5 GPDMA in NORMAL mode, after the last transfer, CSAR/CDAR are **updated
to the address of the next transfer that would have followed**. This is documented
in RM0481 but differs from older DMA controllers where addresses are preserved.

After the 12-byte manual test:
- RX CDAR: `0x20000800 + 12 = 0x2000080C` (past end of `ads_rx_dma`)
- TX CSAR: `0x200007F4 + 12 = 0x20000800` (which is `ads_rx_dma`, not `ads_tx_dma`!)

**`ads_fast_start()` never reloaded CSAR/CDAR.** Every DRDY-triggered transfer:
- RX DMA wrote 12 bytes past the buffer → **silent RAM corruption**
- TX DMA read from `ads_rx_dma` instead of `ads_tx_dma` → wrong TX data

This bug has existed since the register-level DMA was introduced. It was masked
in the original HAL-based DMA because HAL reloads all addresses each call.

### Fix Applied

Added CDAR/CSAR reload in `ads_fast_start()` (ISR hot path):
```c
dma_rx->CDAR = (uint32_t)ads_rx_dma;   // reload RX dest to buffer start
dma_tx->CSAR = (uint32_t)ads_tx_dma;   // reload TX src to buffer start
```

Also added to the hard cleanup in `start_continuous()` before DRDY enable.

Two extra register writes per transfer (~6 cycles at 75 MHz = ~80 ns). Negligible
overhead vs the 15.625 µs DRDY period.
