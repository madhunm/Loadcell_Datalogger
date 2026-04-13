# ADS131M02 Driver & Test Suite — Final State Document

**Date:** 2026-04-12 (updated post-Phase 7 completion + SYSCLK upgrade)
**SYSCLK:** 250 MHz (VOS0, PLL1: PLLM=4, PLLN=31, FRACN=2048, PLLP=2)
**Status:** Zero-loss 64 kSPS continuous DMA capture — **VERIFIED ON HARDWARE**

---

## 1. Architecture Overview

```
   ADS131M02 (delta-sigma ADC, 2 ch, 64 kSPS)
       |                |
   DRDY (PB2/EXTI2)    SPI1 (PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI)
       |                   |
   EXTI2 ISR            GPDMA1 CH0 (TX) + CH1 (RX)
  [ads_fast_drdy_handler]     |
       |                GPDMA1_CH1 TC IRQ
       |            [ads_fast_dma_complete_handler]
       |                   |
       +----> ads_dma_stats_t <----+
                   |
           main loop reads stats
           1 Hz telemetry via UART1 printf
           VT220 CDC UI via USB
```

### Hardware Connections

| Signal    | MCU Pin | Function                              |
|-----------|---------|---------------------------------------|
| SPI1_SCK  | PA5     | SPI clock, 12.5 MHz (PLL1Q / 4)      |
| SPI1_MISO | PA6     | ADC data out (DOUT)                   |
| SPI1_MOSI | PA7     | ADC command in (DIN)                  |
| ADC_CS    | PA4     | Chip select (active low, GPIO-driven) |
| ADC_DRDY  | PB2     | Data ready → EXTI2 falling edge       |
| ADC_Reset | PA3     | Hardware reset (active low)           |
| DRDY_HW   | PB4     | TIM3 CH1 external clock (edge count)  |
| LTC_CS    | PA1     | LTC6903 oscillator CS (shared SPI bus)|

### Clock Tree (verified on hardware)

```
HSI 64 MHz → PLL1 (PLLM=4 → 16 MHz ref)
    PLLN=31, FRACN=2048 → VCO = 500.0 MHz
    PLLP=/2 → SYSCLK = 250 MHz (VOS0, FLASH_LATENCY_5, ICACHE=ON)
    PLLQ=/10 → PLL1Q = 50 MHz → SPI1 kernel clock
    PLLR=/2 → 250 MHz (unused)

PLL2P = 20 MHz → SPI2 kernel clock (SD card)

SPI1 prescaler = /4 → SCK = 12.5 MHz
```

**Key fact:** PLL1Q = 50 MHz is independent of the SYSCLK upgrade (was 150/3=50 at 75 MHz, now 500/10=50 at 250 MHz). SPI1 SCK has always been 12.5 MHz.

### SPI1 Configuration (from `MX_SPI1_Init` in `spi.c`)

| Parameter         | Value                    | Register Decode              |
|-------------------|--------------------------|------------------------------|
| Mode              | Master, Full Duplex      | CFG2                         |
| Data Size         | 8-bit                    | CFG1.DSIZE = 7               |
| CLK Polarity      | Low (CPOL=0)             | CFG2.CPOL = 0                |
| CLK Phase         | 2nd edge (CPHA=1)        | CFG2.CPHA = 1 → SPI Mode 1  |
| NSS               | Software (GPIO CS)       | CFG2.SSM = 1                 |
| Baud Prescaler    | /4                       | CFG1.MBR = 001               |
| First Bit         | MSB                      | CFG2.LSBFRST = 0             |
| FIFO Threshold    | 1 data                   | CFG1.FTHLV = 0               |
| Frame Size        | 12 bytes (4 × 24-bit)   | CR2.TSIZE = 12               |
| Kernel Clock      | PLL1Q (50 MHz)           | RCC_SPI1CLKSOURCE_PLL1Q      |

**Verified CFG1 register on hardware:** `0x10070007`
- MBR = 001 (/4 prescaler)
- DSIZE = 7 (8-bit)
- FTHLV = 0 (1 data FIFO threshold)
- RXDMAEN = 0, TXDMAEN = 0 (controlled by driver at runtime)

### DMA Configuration (CubeMX + HAL_SPI_MspInit)

| Channel          | Direction          | Request | Increment          | Data Width |
|------------------|--------------------|---------|--------------------|-----------:|
| GPDMA1_Channel0  | Memory → SPI1_TX   | REQSEL=7 (SPI1_TX) | SRC_INC, DST_FIXED | BYTE |
| GPDMA1_Channel1  | SPI1_RX → Memory   | REQSEL=6 (SPI1_RX) | SRC_FIXED, DST_INC | BYTE |

- **Mode:** DMA_NORMAL (single block; CCR.EN auto-clears on completion)
- **Priority:** LOW_PRIORITY_HIGH_WEIGHT on both
- **Transfer Event:** DMA_TCEM_BLOCK_TRANSFER
- **TC Interrupt:** RX channel only (CCR.TCIE set by driver)

**Critical GPDMA behavior (RM0481):** After a DMA_NORMAL transfer completes:
- CCR.EN auto-clears; CTR1, CTR2 persist
- **CSAR/CDAR advance** past the last transferred byte (must reload before restart)
- CBR1.BNDT decrements to 0 (must reload)
- TCF is set in CSR

### NVIC Priorities (final configuration)

| IRQ                       | Priority | Runtime State        | Purpose                                |
|---------------------------|----------|----------------------|----------------------------------------|
| EXTI2 (DRDY)             | 0        | Enabled              | DRDY falling edge → DMA start          |
| GPDMA1_Channel0 (TX)     | 0        | **Disabled by driver** | TX DMA complete (not needed)         |
| GPDMA1_Channel1 (RX)     | 0        | Enabled              | RX DMA TC → sample extraction          |
| SPI1                      | 1        | **Disabled by driver** | SPI error (IER=0, NVIC off)         |
| SDMMC1                    | 5        | Enabled              | SD card                                |
| USB_DRD_FS                | 6        | Enabled              | USB CDC                                |
| USART1                    | 7        | Enabled              | UART debug                             |
| EXTI4                     | 8        | Enabled              | Log start button                       |
| TIM3                      | 8        | Enabled              | DRDY edge counter overflow             |
| TIM8_UP                   | 10       | Enabled              | CLKIN counter overflow                 |

**NVIC gating rationale:** TX DMA and SPI1 NVIC are disabled in `ads_dma_setup()` to prevent HAL from processing register-level DMA flags. Only EXTI2 (DRDY trigger) and GPDMA1_CH1 (RX TC) remain active during continuous capture.

---

## 2. Source Files

### Core driver: `Core/Src/adc_ads131m02.c` (857 lines)

**Phase 6 — Blocking register access (SPI polling):**

| Function                  | Purpose                                                      |
|---------------------------|--------------------------------------------------------------|
| `ads_transfer_frame()`    | Send 4-word SPI frame, extract 16-bit response from DOUT[0]  |
| `ads_wreg_frame()`        | Send WREG command + data in word 1                           |
| `ads131m02_read_reg()`    | Two-frame pipelined register read (RREG → NULL)              |
| `ads131m02_write_reg()`   | Write + 1 ms delay + readback verify                         |
| `ads_hw_reset()`          | PA3 LOW 2 ms → HIGH, 50 ms settle                           |
| `ads131m02_init()`        | HW reset, UNLOCK, configure MODE/CLOCK/GAIN, verify readback |
| `ads131m02_dump_regs()`   | Print 8 key registers (ID through CH1_CFG)                   |

**Phase 7 — DMA hot path (register-level GPDMA + SPI):**

| Function / Variable            | Purpose                                                           |
|---------------------------------|-------------------------------------------------------------------|
| `ads_dma_setup()`               | One-time: DMA channels, DWT enable, SPI FIFO flush, NVIC gating  |
| `ads_fast_start()`              | ISR inline: CS LOW, reload CDAR/CSAR/CBR1/CFCR, enable DMA+SPI   |
| `ads_fast_complete()`           | ISR inline: TXC wait, guard NOPs, CS HIGH, SPE=0, extract samples |
| `ads_spi_force_clean_idle()`    | SPE 1→0 toggle to force-flush SPI FIFOs + clear flags             |
| `ads_fast_drdy_handler()`       | EXTI2 ISR: clear pending, count, start DMA if not busy            |
| `ads_fast_dma_complete_handler()` | GPDMA1_CH1 ISR: error/skip/normal paths, DWT instrumentation   |
| `ads_manual_dma_test()`         | Single polled DMA transfer (CH1 NVIC off, DWT timeout 100 ms)     |
| `ads131m02_start_continuous()`  | 3-attempt startup: setup → manual test → hard cleanup → EXTI2 on  |
| `ads131m02_stop_continuous()`   | Gate NVIC, disable DMA, IDLEF wait, flush SPI, restore HAL state  |
| `ads131m02_clear_stats()`       | Zero all stats with IRQ disabled                                  |
| `ads_dump_dma_regs()`           | Debug: print DMA RX/TX + SPI register dump (unused in production) |
| `ads_dma_busy`                  | `volatile uint8_t`, ISR race guard                                |
| `ads_dma_stop`                  | `volatile uint8_t`, initialized to 1, gates DRDY handler          |
| `ads_skip_frames`               | `volatile uint8_t`, discard N frames after mode transitions        |

**Test suite:**

| Function                        | Purpose                                                           |
|---------------------------------|-------------------------------------------------------------------|
| `ads131m02_polarity_test(dur)`  | Alternating +/− DC test signal, per-window accumulators, symmetry error |
| `ads131m02_drdy_fmt_test(arm_s)` | A/B comparison: DRDY level mode vs pulse mode miss rates          |

### Header: `Core/Inc/adc_ads131m02.h` (150 lines)

Contains: register address defines, command opcodes, RREG/WREG macros, STATUS bit masks, ID mask, MODE/CLOCK/GAIN field symbols, `ADS_MODE_INIT` composite, `ADS_CLOCK_64KSPS`, CHn_CFG MUX values, frame geometry constants, `ads_dma_stats_t` struct, public API declarations, `ads_sign_extend_24()` inline.

### ISR glue: `Core/Src/stm32h5xx_it.c`

```c
void EXTI2_IRQHandler(void) {
    ads_fast_drdy_handler();
    return;                              // bypass HAL_GPIO_EXTI_IRQHandler
}

void GPDMA1_Channel0_IRQHandler(void) {
    GPDMA1_Channel0->CFCR = DMA_CFCR_ALL;  // clear all flags
    return;                                  // never call HAL_DMA_IRQHandler
}

void GPDMA1_Channel1_IRQHandler(void) {
    if (!ads_fast_dma_complete_handler())
        GPDMA1_Channel1->CFCR = DMA_CFCR_ALL;  // fallback: clear flags
    return;                                       // never call HAL_DMA_IRQHandler
}
```

All three handlers use `return` before the CubeMX-generated HAL calls. HAL never processes DMA/SPI/EXTI during continuous capture.

### Diagnostics: `Core/Src/diag_timers.c` + `Core/Inc/diag_timers.h`

| Function                        | Purpose                                                          |
|---------------------------------|------------------------------------------------------------------|
| `diag_clkin_init()`             | TIM8 external clock mode 1 on PC6/TI1FP1 (LTC6903 CLKIN edges)  |
| `diag_clkin_poll()`             | 1-second delta → cached Hz, feeds UI                             |
| `diag_clkin_measure_hz(ms)`     | Blocking DWT-based frequency measurement for auto-trim           |
| `diag_clkin_stability_test(s)`  | Per-second LTC6903 + SYSCLK cross-reference stability analysis   |
| `diag_drdy_init()`              | TIM3 external clock mode 1 on PB4/TI1FP1 (DRDY falling edges)   |
| `diag_drdy_read_edges()`        | Atomic 32-bit read: `ovf * 65536 + CNT` with overflow race guard |
| `diag_drdy_tim3_overflow()`     | Called from TIM3_IRQHandler (priority 8)                         |

### Debug output: `Core/Src/debug_uart.c`

- `printf()` → `_write()` → UART1 blocking HAL transmit at **921600 baud**
- `cdc_printf()` → USB CDC non-blocking write via USBX
- USART1_BRR = 0x10F (verified on hardware at 250 MHz)

### VT220 CDC UI: `Core/Src/debug_ui.c` + `Core/Inc/debug_ui.h`

Active in main loop:
- `cdc_poll()` — USB CDC keepalive
- `ui_update_fields()` — refresh VT220 panel fields at 100 ms / 1000 ms intervals
- `ui_process_input()` — handle user keyboard input

UI fields populated from main loop 1 Hz block:
- `ui_set_drdy_hz(sw_d)` — DRDY rate in Hz
- `ui_set_adc_counts(ch0, ch1)` — raw 24-bit signed ADC counts
- `ui_set_adc_ring(sw, dma, miss)` — per-second ring stats
- `ui_set_vratio(v_ch0)` — converted voltage (Vref=1.2V, 24-bit)
- `ui_set_usb_status()` / `ui_set_sd_status()` — peripheral state strings

---

## 3. Register Configuration

### ADS131M02 Registers (post-init verified values)

| Register | Address | Value  | Meaning                                           |
|----------|---------|--------|---------------------------------------------------|
| ID       | 0x00    | 0x2205 | ADS131M02, 2-channel, silicon rev 5               |
| STATUS   | 0x01    | 0x0103 | DRDY0 + DRDY1 set, no errors                      |
| MODE     | 0x02    | 0x0111 | 24-bit words, DRDY pulse mode, SPI timeout ON     |
| CLOCK    | 0x03    | 0x0322 | CH0+CH1 enabled, TBM=1, OSR=0, HR mode = 64 kSPS |
| GAIN1    | 0x04    | 0x0000 | PGA gain = 1× both channels                       |
| CFG      | 0x06    | 0x0600 | Default (GC_DLY=6, no GC_EN)                      |
| CH0_CFG  | 0x09    | 0x0000 | External analog inputs (MUX=0)                     |
| CH1_CFG  | 0x0E    | 0x0000 | External analog inputs (MUX=0)                     |

### MODE Register Breakdown (0x0111)

```
Bit [0]     DRDY_FMT   = 1  (pulse mode — fixed-width negative pulse per conversion)
Bit [1]     DRDY_HiZ   = 0  (push-pull output)
Bit [2]     DRDY_SEL   = 0  (most lagging enabled channel)
Bit [3]     Reserved   = 0
Bit [4]     TIMEOUT    = 1  (SPI timeout enabled — auto-recovers from stuck CS)
Bit [7:5]   CRC bits   = 0  (CRC disabled: REG_CRC_EN=0, RX_CRC_EN=0, CRC_TYPE=0)
Bit [8]     WLENGTH    = 1  (24-bit word mode)
Bit [15:9]  Reserved   = 0
```

### Symbolic Build (in `adc_ads131m02.h`)

```c
#define ADS_MODE_INIT  (ADS_MODE_WLENGTH_24 | ADS_MODE_TIMEOUT_EN | ADS_MODE_DRDY_PULSE)
                    //  0x0100              | 0x0010              | 0x0001 = 0x0111
```

### CLOCK Register Breakdown (0x0322)

```
Bit [0]     CH0_EN     = 1  (Channel 0 enabled)
Bit [1]     CH1_EN     = 1  (Channel 1 enabled)
Bit [4:2]   OSR[2:0]   = 0  (OSR select, overridden by TBM)
Bit [5]     TBM        = 1  (Turbo Burst Mode — forces OSR=64)
Bit [7:6]   PWR[1:0]   = 0  (unused when TBM=1)
Bit [9:8]   PWR ext    = 11 (HR = High Resolution mode)
→ fDATA = fCLKIN / (4096 × OSR_factor) = 8192000 / (4096 × 1/32) = 64000 SPS
```

---

## 4. Data Flow — One DMA Cycle (Hot Path)

```
 ┌─ EXTI2 ISR: ads_fast_drdy_handler() ────────────────────────┐
 │  1. DWT->CYCCNT → cyc_entry                                │
 │  2. EXTI->FPR1 = ADC_DRDY_Pin         (clear pending)      │
 │  3. ads_stats.drdy_count++                                  │
 │  4. if ads_dma_stop → goto out                              │
 │  5. if ads_dma_busy → miss_count++, goto out                │
 │  6. ads_dma_busy = 1, dma_start_count++                     │
 │  7. ads_fast_start():                                       │
 │       GPIOA->BSRR = CS_Pin << 16          (CS LOW)         │
 │       dma_rx->CFCR = ALL, CBR1=12, CDAR=&ads_rx_dma        │
 │       dma_tx->CFCR = ALL, CBR1=12, CSAR=&ads_tx_dma        │
 │       SET_BIT(dma_rx->CCR, EN)                              │
 │       SET_BIT(dma_tx->CCR, EN)                              │
 │       SPI1->IFCR = ALL, TSIZE=12                            │
 │       SET_BIT(CFG1, RXDMAEN | TXDMAEN)                     │
 │       SET_BIT(CR1, SPE)                                     │
 │       SET_BIT(CR1, CSTART)                                  │
 │  8. out: DWT peak tracking                                  │
 └─────────────────────────────────────────────────────────────┘
           ↓ (SPI hardware clocks 12 bytes via DMA, ~7.68 µs)
 ┌─ GPDMA1_CH1 ISR: ads_fast_dma_complete_handler() ──────────┐
 │  1. DWT->CYCCNT → cyc_entry                                │
 │  2. Read CSR; if !(TCF):                                    │
 │       → error path: clear flags, SPE=0, CS HIGH, err++      │
 │  3. If ads_skip_frames > 0:                                 │
 │       → skip path: TXC wait, guard NOPs, CS HIGH, SPE=0     │
 │  4. Normal path → ads_fast_complete():                      │
 │       while (!(SR & TXC)) NOP;      (wait last SPI byte)   │
 │       __DSB(); 16× __NOP();          (guard delay ~64 ns)   │
 │       GPIOA->BSRR = CS_Pin           (CS HIGH)             │
 │       CLEAR_BIT(CR1, SPE)                                   │
 │       CLEAR_BIT(CFG1, RXDMAEN | TXDMAEN)                   │
 │       SPI1->IFCR = ALL                                      │
 │       dma_rx->CFCR = ALL, dma_tx->CFCR = ALL               │
 │       Extract CH0/CH1 from ads_rx_dma[] (24-bit BE → signed)│
 │       Update ch0_latest, ch1_latest, dma_count              │
 │       If accumulate: update min/max/sum/count               │
 │       ads_dma_busy = 0                                      │
 │  5. DWT peak tracking                                       │
 └─────────────────────────────────────────────────────────────┘
```

### Timing Budget (250 MHz, verified)

```
EXTI ISR body:               242 cyc =  0.97 µs
NVIC entry+exit (×2):        ~64 cyc =  0.26 µs
SPI DMA transfer (12B@12.5M): wall   =  7.68 µs  (1920 CPU cycles)
DMA ISR body:                311 cyc =  1.24 µs
────────────────────────────────────────────────
Total busy time:            ~2537 cyc = 10.15 µs
DRDY period (64 kSPS):      3906 cyc = 15.625 µs
Margin:                     ~1369 cyc =  5.48 µs  ← comfortable
```

### SPI Frame Layout (24-bit word mode, 12 bytes)

```
Byte:  [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]  [8]  [9] [10] [11]
Word:  ─── Word 0 ───  ─── Word 1 ───  ─── Word 2 ───  ─── Word 3 ───
       STATUS (16b)+0   CH0 (24b, BE)   CH1 (24b, BE)   CRC (disabled)

TX: Command in bytes [0:1], rest zero
RX: CH0 = (rx[3]<<16 | rx[4]<<8 | rx[5]), sign-extend from 24-bit
    CH1 = (rx[6]<<16 | rx[7]<<8 | rx[8]), sign-extend from 24-bit
```

---

## 5. Statistics Structure (`ads_dma_stats_t`)

```c
typedef struct {
    volatile uint32_t drdy_count;       // Total DRDY edges seen by EXTI2
    volatile uint32_t dma_count;        // Successful DMA completions (frames extracted)
    volatile uint32_t dma_start_count;  // DMA transfers initiated
    volatile uint32_t miss_count;       // DRDYs skipped because ads_dma_busy=1
    volatile uint32_t dma_error_count;  // DMA IRQs without TCF (error recovery path)
    volatile int32_t  ch0_latest;       // Last CH0 sample (24-bit signed, −8388608..+8388607)
    volatile int32_t  ch1_latest;       // Last CH1 sample

    volatile uint32_t max_exti_cycles;  // Peak DWT cycles in EXTI2 handler (lifetime)
    volatile uint32_t max_dma_cycles;   // Peak DWT cycles in DMA complete handler (lifetime)

    /* Per-window accumulator (enabled during polarity test) */
    volatile int32_t  ch0_min, ch0_max;
    volatile int64_t  ch0_sum;
    volatile int32_t  ch1_min, ch1_max;
    volatile int64_t  ch1_sum;
    volatile uint32_t window_count;     // Samples in current accumulation window
    volatile uint8_t  accumulate;       // 1 = ISR updates accumulators
} ads_dma_stats_t;
```

**Invariants:**
- `drdy_count = dma_start_count + miss_count + (DRDYs during stop/before start)`
- `dma_count = dma_start_count − dma_error_count − skip_frames consumed`
- `dma_error_count` = 0 in steady state (non-zero indicates a bug)

---

## 6. Startup Sequence (detailed)

### `ads131m02_start_continuous()` — 3-attempt retry loop

```
For attempt 0..2:
  1. ads_dma_setup():
       a. dma_rx = GPDMA1_Channel1, dma_tx = GPDMA1_Channel0
       b. Enable DWT cycle counter (CoreDebug->DEMCR, DWT->CTRL)
       c. Zero TX buffer
       d. Disable both DMA channels, wait IDLEF on each
       e. Clear all DMA flags on both channels
       f. Program DMA: CSAR, CDAR, CBR1 (12 bytes), TCIE on RX
       g. ads_spi_force_clean_idle():
            - CLEAR_BIT(CFG1, RXDMAEN | TXDMAEN)
            - SET_BIT(CR1, SPE)           ← force SPE=1
            - CLEAR_BIT(CR1, SPE)         ← 1→0 transition flushes FIFOs
            - SPI1->IFCR = ALL
       h. SPI1->IER = 0 (no SPI interrupts)
       i. MODIFY_REG(CR2, TSIZE, 12)
       j. HAL_NVIC_DisableIRQ(GPDMA1_Channel0_IRQn)
       k. HAL_NVIC_DisableIRQ(SPI1_IRQn)
       l. NVIC_ClearPendingIRQ on CH0, CH1, SPI1

  2. ads_manual_dma_test():
       a. HAL_NVIC_DisableIRQ(GPDMA1_Channel1_IRQn)
       b. NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn)
       c. __disable_irq(); ads_fast_start(); __enable_irq()
       d. Poll GPDMA1_Channel1->CSR for TCF, DWT 100 ms timeout
       e. On success: ads_fast_complete(), print CH0/CH1
       f. On timeout: full cleanup (CS HIGH, CFG1, SPE, DMA disable, IFCR, CFCR)
       g. NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn)  ← prevents stale TC IRQ
       h. HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn)

  3. If manual test passed → Hard cleanup before DRDY-triggered mode:
       a. HAL_NVIC_DisableIRQ(EXTI2_IRQn)
       b. ads_spi_force_clean_idle()
       c. Disable both DMA channels, wait IDLEF
       d. Clear all DMA flags, reload CBR1, CDAR, CSAR
       e. Reload TSIZE
       f. Clear EXTI2 pending, NVIC pending on EXTI2 + CH1
       g. ads_skip_frames = 2
       h. ads_dma_busy = 0, ads_dma_stop = 0
       i. HAL_NVIC_EnableIRQ(EXTI2_IRQn) → DRDY-triggered path is live

  4. If all 3 attempts fail: ads_dma_stop = 1, print error
```

### `ads131m02_stop_continuous()`

```
1. ads_dma_stop = 1
2. HAL_NVIC_DisableIRQ(EXTI2_IRQn + GPDMA1_CH1)
3. __DSB(); __ISB()
4. Disable both DMA channels, wait IDLEF
5. ads_spi_force_clean_idle()
6. CS HIGH
7. Clear all DMA flags
8. ads_dma_busy = 0
9. NVIC_ClearPendingIRQ on EXTI2 + CH1
10. HAL_NVIC_EnableIRQ(CH1 + EXTI2)
11. Restore HAL state: hspi1.State = READY, both DMA handles = READY
```

Step 11 is required because register-level DMA bypasses HAL state tracking. Without it, subsequent blocking `HAL_SPI_TransmitReceive()` calls in `ads131m02_write_reg()` would fail with HAL_BUSY.

### Full Boot Sequence (main.c)

```
 1. HAL_Init(), SystemClock_Config() → 250 MHz VOS0
 2. GPIO, GPDMA, ICACHE, USART1 init
 3. Boot diagnostics: DEV_ID, REV_ID, SYSCLK, VOS, FLASH_LAT, HCLK/APB1/APB2, PLL1, ICACHE
 4. HAL_NVIC_DisableIRQ(EXTI2_IRQn) — suppress DRDY during SPI setup
 5. USBX/CDC init, SPI1+SPI2 init, TIM8 init, TIM3 init
 6. SPI1 diagnostics: CFG1, MBR, kernel clock, SCK frequency
 7. NVIC priority overrides
 8. CS pins HIGH (ADC_CS, LTC_CS)
 9. LTC6903 init (Mode 0 → Mode 1 SPI polarity switch) + auto-trim (8.192 MHz)
10. ADS131M02 init (HW reset, register config, verify)
11. USB CDC wait (5 s) → report USB/SD status
12. SDMMC + FatFS init → report SD capacity/free
13. Re-enable EXTI2
14. diag_drdy_init() (TIM3 edge counter)
15. ads131m02_start_continuous() (manual test + DRDY enable)
16. 500 ms warmup (cooperative wait with ux_system_tasks_run)
17. Polarity test (60 s)
18. DRDY_FMT A/B test (10 s per arm)
19. UI draw + log init
20. Main loop: 1 Hz telemetry + UI updates + CDC poll
```

---

## 7. Test Suite

### 7.1 Manual DMA Test (`ads_manual_dma_test`)

**Purpose:** Validate the SPI+DMA register-level path works before enabling ISR-driven mode.

**Key design decisions:**
- CH1 NVIC is **disabled** during the test to prevent the ISR from consuming TCF before the polling loop — this eliminates the ISR/polling race that caused apparent intermittent failures (Bug 4).
- Timeout uses **DWT->CYCCNT** (100 ms), not HAL_GetTick(), to be immune to SysTick starvation under high interrupt load.
- After `ads_fast_complete()`, `NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn)` clears the NVIC pending bit that was latched when TCF fired (even though the NVIC line was disabled, the flag is still latched). Without this, the first DRDY-triggered DMA complete would see no TCF and increment `dma_error_count`.

**Run context:** Called from `ads131m02_start_continuous()`, up to 3 attempts. On hardware at 250 MHz, succeeds on first attempt every boot.

### 7.2 Polarity Test (`ads131m02_polarity_test`)

**Purpose:** Validate ADC accuracy and channel symmetry using internal DC test signals.

**Sequence (per second for `duration_s` seconds):**
1. `stop_continuous()`, set CH0/CH1 MUX to alternating +test/−test
2. `start_continuous()` (includes manual DMA test), enable ISR accumulator
3. Wait 1 second (ISR accumulates min/max/sum/count into `ads_stats`)
4. Snapshot stats, fold into per-polarity summary buckets (separate +/− accumulators)
5. After all windows: print per-polarity avg/spread, symmetry error percentage
6. Revert MUX to external analog inputs, clear stats, restart continuous

**Expected values (verified at 250 MHz):**
- +test avg: ~+1,080,000 counts (24-bit signed)
- −test avg: ~−1,078,000 counts
- Symmetry error: < 0.25%
- Spread per window: < 5,000 counts

### 7.3 DRDY_FMT A/B Test (`ads131m02_drdy_fmt_test`)

**Purpose:** Compare level mode (DRDY_FMT=0) vs pulse mode (DRDY_FMT=1) miss rates.

**Sequence:**
1. Arm A: write MODE with DRDY_FMT=0 (level), clear stats, run `arm_seconds`, print counters
2. Arm B: write MODE with DRDY_FMT=1 (pulse), clear stats, run `arm_seconds`, print counters
3. Revert MODE to `ADS_MODE_INIT` (pulse mode), clear stats, restart continuous

**Level mode note:** In level mode, DRDY re-asserts while CS is LOW and data is being clocked out, causing inflated SW DRDY counts (EXTI re-triggers). At 250 MHz with register-level DMA, both modes achieve **zero misses** — but pulse mode is preferred because `sw == hw` (no inflation).

---

## 8. Performance — Final Hardware Results (250 MHz)

### Steady-State (from `Serisl_Debug.txt`, continuous 1 Hz samples)

```
ADC: sw=63913 hw=63913 ok=63912 miss=0 err=0 exti=242 dma=307
ADC: sw=63982 hw=63982 ok=63982 miss=0 err=0 exti=242 dma=307
ADC: sw=64004 hw=64004 ok=64005 miss=0 err=0 exti=242 dma=311
ADC: sw=63949 hw=63949 ok=63949 miss=0 err=0 exti=242 dma=311
ADC: sw=63963 hw=63963 ok=63962 miss=0 err=0 exti=242 dma=311
ADC: sw=63992 hw=63992 ok=63992 miss=0 err=0 exti=242 dma=311
ADC: sw=63939 hw=63939 ok=63940 miss=0 err=0 exti=242 dma=311
ADC: sw=63965 hw=63965 ok=63965 miss=0 err=0 exti=242 dma=311
ADC: sw=63980 hw=63980 ok=63979 miss=0 err=0 exti=242 dma=311
```

| Metric                 | Value             | Notes                                    |
|------------------------|-------------------|------------------------------------------|
| DRDY seen (sw)         | 63,913–64,004/s   | Pulse mode, clean edges                  |
| DRDY hardware (hw)     | = sw              | TIM3 matches EXTI2 perfectly             |
| DMA ok                 | = sw (±1)         | Zero-loss capture                        |
| Miss                   | **0**             | Continuously over 60+ seconds            |
| Error                  | **0**             | No DMA errors (stale NVIC bug fixed)     |
| EXTI ISR peak          | 242 cycles        | 0.97 µs at 250 MHz                      |
| DMA ISR peak           | 311 cycles        | 1.24 µs at 250 MHz                      |

### Polarity Test Results (250 MHz, 60 seconds)

```
CH0 +test: avg=+1080222 spread=4665 (30 windows)
CH0 -test: avg=-1078359 spread=4410 (30 windows)
CH1 +test: avg=+1081031 spread=4919 (30 windows)
CH1 -test: avg=-1078546 spread=4579 (30 windows)
Symmetry error: CH0=+0.1725%  CH1=+0.2299%
Total DRDY=3877727 DMA=3868686 miss=0
```

Both channels produce correct, symmetric, stable DC test signal readings. Symmetry error well below the 0.25% threshold. 3.87 million DRDY events processed with **zero misses** over 60 seconds.

### DRDY_FMT A/B Test Results (250 MHz)

```
[A] DRDY_FMT=0 (level), 10 s:
  DRDY_SW=639731  DRDY_HW=639731  DMA=639705  miss=0

[B] DRDY_FMT=1 (pulse), 10 s:
  DRDY_SW=639748  DRDY_HW=639748  DMA=639734  miss=0
```

Both modes achieve zero misses at 250 MHz. Level mode shows inflated DRDY counts in the earlier 75 MHz baseline but not here (register-level DMA is fast enough that the brief CS LOW period doesn't trigger a DRDY re-assertion in most cases).

### Historical Comparison: 75 MHz vs 250 MHz

| Metric            | 75 MHz (VOS3)  | 250 MHz (VOS0)   | Improvement         |
|-------------------|----------------|-------------------|---------------------|
| EXTI ISR peak     | 334 cyc / 4.45 µs | 242 cyc / 0.97 µs | 4.6× faster      |
| DMA ISR peak      | 367 cyc / 4.89 µs | 311 cyc / 1.24 µs | 3.9× faster      |
| Total busy time   | ~15.32 µs      | ~10.15 µs         | 34% reduction       |
| Margin            | 0.31 µs (2%)   | 5.48 µs (35%)     | 17.7× more headroom |
| Miss rate (pulse) | 33% (16K/48K)  | **0%**            | Eliminated          |
| Miss rate (level) | 49%            | **0%**            | Eliminated          |
| DMA errors        | 1 (benign)     | **0**             | Fixed               |

The ISR cycle counts decreased despite higher clock because ICACHE eliminates flash wait-state stalls on instruction fetch.

---

## 9. Resolved Issues — Full Troubleshooting History

These 7 bugs were discovered across 8 test runs and 2 debugging sessions. Full details in [PHASE7_DMA_CRASH_CONTEXT.md](PHASE7_DMA_CRASH_CONTEXT.md).

| # | Bug | Root Cause | Fix | Test Run |
|---|-----|-----------|-----|----------|
| 1 | HAL DMA miss rate 75% | `HAL_SPI_TransmitReceive_DMA()` takes ~47 µs per call (GPDMA overhead, HAL locks, callback dispatch) | Replaced with register-level GPDMA+SPI writes. Total ISR overhead dropped from 47 µs to ~3 µs. | Run 1 |
| 2 | System hang after register-level DMA enable | Stale data in SPI1 RX FIFO from prior `HAL_SPI_TransmitReceive()` calls. `CLEAR_BIT(SPE)` when SPE was already 0 does NOT flush FIFOs — RM0481 requires a 1→0 transition. Stale RXP=1 triggered immediate spurious RX DMA, corrupting TSIZE. | Force-flush: `SET_BIT(SPE); CLEAR_BIT(SPE)` in `ads_dma_setup()` — now `ads_spi_force_clean_idle()`. | Runs 2–4 |
| 3 | HardFault under DRDY load | TX DMA channel (GPDMA1_CH0) NVIC enabled by CubeMX. Register-level TX DMA completion set TCF, `HAL_DMA_IRQHandler` processed with stale HAL state, corrupted SPI/DMA pipeline. | Intercept both GPDMA handlers in `stm32h5xx_it.c` — unconditional flag-clear-and-return. Disable CH0 NVIC in `ads_dma_setup()`. | Run 5 |
| 4 | Intermittent manual DMA test failure | `ads_manual_dma_test()` polled for TCF, but GPDMA1_CH1 NVIC was enabled. ISR consumed TCF before polling loop, making every transfer appear as a timeout. 3rd attempt "succeeded" by timing luck. | Disable CH1 NVIC during manual test, poll TCF directly, re-enable after. Use DWT CYCCNT timeout (SysTick-immune). | Run 6 |
| 5 | DRDY-triggered path stalls after successful manual test | `ads_fast_complete()` cleared RXDMAEN/TXDMAEN in CFG1 while SPE=1. Per RM0481, CFG1 writes are silently ignored when SPE=1. DMA enables remained set, next `ads_fast_start()` saw stale RX FIFO data. | Reorder shutdown: `SPE=0` first (flushes FIFOs, makes CFG1 writable), then clear CFG1. Added hard SPI+DMA cleanup between manual test and DRDY enable. | Run 7 |
| 6 | Silent RAM corruption + wrong TX data | GPDMA CSAR/CDAR auto-advance to next-would-be address after DMA_NORMAL completion. `ads_fast_start()` never reloaded these → RX wrote 12 bytes past buffer, TX read from `ads_rx_dma` instead of `ads_tx_dma`. | Reload `dma_rx->CDAR` and `dma_tx->CSAR` in `ads_fast_start()` every transfer (2 extra register writes, ~80 ns). | Run 8 |
| 7 | Benign `err=1` after boot | `ads_manual_dma_test()` re-enabled CH1 NVIC after `ads_fast_complete()` cleared TCF, but the NVIC pending bit (latched when TCF originally fired while NVIC was disabled) remained set. First DRDY-triggered DMA complete handler saw no TCF → error path. | `NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn)` between `ads_fast_complete()` and `HAL_NVIC_EnableIRQ()` in `ads_manual_dma_test()`. | Post-completion |

### Additional fixes applied during Phase 7:

| Fix | Details |
|-----|---------|
| TIM3 prescaler shadow register | CubeMX set PSC=249 but shadow wasn't loaded. Added `EGR = TIM_EGR_UG` + clear spurious UIF. |
| `ads_dma_stop = 1` init | Prevents DRDY ISR from starting DMA before `ads131m02_start_continuous()`. |
| TSIZE reload per transfer | SPI CR2.TSIZE auto-decrements to 0. Must reload before each new transfer. |
| HAL state fixup in `stop_continuous()` | `hspi1.State = READY`, both DMA handles `= READY`. Required for blocking HAL calls in polarity test. |
| `stop_continuous()` IDLEF wait | Per RM0481: must wait IDLEF=1 after clearing DMA EN before reprogramming. |
| Skip first 2 frames after DRDY enable | `ads_skip_frames = 2`. Discards MUX transition/settling frames after polarity changes. |
| TXC wait before SPE=0 | `while (!(SPI1->SR & SPI_SR_TXC)) __NOP()` — ensures SPI bus is fully finished. |
| DSB + 16-NOP guard delay | Barrier + ~64 ns guard after TXC before CS HIGH. Handles any remaining SCK→MISO propagation. |

---

## 10. STM32H5 Lessons Learned (for posterity)

### SPI Peripheral (RM0481)

1. **SPE 1→0 flushes FIFOs; SPE 0→0 does nothing.** Always toggle SPE high then low to guarantee a clean state. A `CLEAR_BIT(SPE)` when SPE is already 0 is silently a no-op.

2. **CFG1 and CFG2 can only be written when SPE=0.** Writes while SPE=1 are silently ignored — the register retains its old value with no error indication. This includes RXDMAEN and TXDMAEN.

3. **RX DMA TC does not mean "SPI bus finished."** Always wait for `SPI_SR_TXC` before deasserting CS or clearing SPE. On the STM32H5, the last byte may still be shifting out when the DMA RX TC fires.

4. **SPI1->IER should be 0 for register-level DMA.** Any stale interrupt enable can cause `SPI1_IRQHandler` to fire and `HAL_SPI_IRQHandler` to process with unexpected state. Explicitly zero IER and disable SPI1 NVIC.

5. **TSIZE auto-decrements to 0.** Must be reloaded before every transfer.

### GPDMA (RM0481)

6. **CSAR/CDAR advance past the transferred data in DMA_NORMAL mode.** After a 12-byte transfer, CDAR points to `buffer + 12`, not `buffer`. Both addresses must be reloaded in the restart path.

7. **Wait for IDLEF=1 after clearing CCR.EN before reprogramming.** The channel is not immediately idle; internal state machine needs a few cycles to settle.

8. **NVIC pending bits are latched independently of the NVIC enable.** When a DMA TC fires while the NVIC line is disabled, the pending bit is set but the ISR doesn't run. When the NVIC is re-enabled, the stale pending bit fires immediately — even if the TCF flag has been cleared by software. **Always `NVIC_ClearPendingIRQ()` before re-enabling.**

9. **HAL_DMA_IRQHandler processes flags from register-level transfers.** If CubeMX enabled the NVIC for a DMA channel and your code writes to GPDMA registers directly (bypassing HAL), the HAL ISR may fire on completion and interpret the flags with stale internal state. **Either disable the NVIC or intercept the handler.**

### General

10. **DWT->CYCCNT is immune to SysTick starvation.** At 128K IRQ/sec (EXTI2 + GPDMA1_CH1 at 64 kSPS), SysTick at priority 15 can be starved by priority-0 IRQs. HAL_GetTick()-based timeouts may never expire. DWT counts CPU cycles regardless of interrupt state.

11. **`printf()` in foreground while 128K IRQ/sec is active.** Blocking UART transmit at 921600 baud for a 100-char line takes ~1 ms. During that time, ~128 ISR pairs fire. This works because the ISR handlers are extremely lean (~1 µs each) and priority-0, preempting the UART HAL. But verbose printf (multi-line register dumps) can cause observable glitches in UI output.

---

## 11. Design Decisions and Rationale

### Why DRDY pulse mode (not level)

- In level mode, DRDY goes low and stays low until data is clocked out. Each CS LOW + SPI clock activity while DRDY is held low can re-trigger EXTI, inflating the software DRDY count.
- In pulse mode, each conversion produces a single short negative pulse (~1 MCLK cycle = ~122 ns). Clean edge for EXTI, clean edge for TIM3 hardware counter.
- The tradeoff: if a pulse is missed, the ADC skips that conversion. This is acceptable because the goal is to service every DRDY, and TIM3 immediately reveals any missed edges (`hw > sw`).

### Why register-level DMA (not HAL)

- `HAL_SPI_TransmitReceive_DMA()` overhead: ~40–47 µs per call on STM32H5 GPDMA. Dominated by HAL lock/unlock, DMA init checks, callback dispatch, SPI state management.
- DRDY period: 15.625 µs. HAL overhead alone exceeds the budget by 3×.
- Register-level restart: ~0.97 µs (EXTI handler) + ~1.24 µs (DMA complete handler). Only the SPI bus transfer time (7.68 µs) is unavoidable.

### Why skip first 2 frames

- After a MUX change (polarity test), the ADS131M02 needs up to 2 conversion cycles for the digital filter to settle with the new input. The first 1-2 frames may contain transition data.
- `ads_skip_frames = 2` discards these without affecting the accumulator.

### Why 3-attempt retry in `start_continuous()`

- Historical: the first 1-2 manual DMA tests could fail due to residual internal DMA state from HAL initialization (Bug 4 pre-fix). With the ISR/polling race fix, the first attempt now succeeds consistently. The retry loop is retained as defensive code.

---

## 12. File Reference

| File                                 | Lines | Purpose                                                    |
|--------------------------------------|------:|------------------------------------------------------------|
| `Core/Inc/adc_ads131m02.h`          | 150   | Register defines, stats struct, public API, inline helpers |
| `Core/Src/adc_ads131m02.c`          | 857   | Full driver: blocking regs, DMA hot path, test suite       |
| `Core/Src/stm32h5xx_it.c`           | ~280  | EXTI2 + GPDMA1_CH0/CH1 ISR glue (bypass HAL)              |
| `Core/Src/diag_timers.c`            | 411   | TIM3 DRDY counter, TIM8 CLKIN counter, stability test      |
| `Core/Inc/diag_timers.h`            | 28    | Diagnostics public API                                     |
| `Core/Src/spi.c`                     | ~208  | SPI1 init + DMA channel init (CubeMX generated)            |
| `Core/Src/main.c`                    | ~400  | Boot sequence, main loop telemetry, UI feeding              |
| `Core/Src/debug_uart.c`             | ~50   | `printf` → UART1, `cdc_printf` → USB CDC                  |
| `Core/Src/debug_ui.c`               | ~496  | VT220 terminal UI panel + keyboard input                   |
| `Core/Inc/debug_ui.h`               | ~50   | UI setter functions (`ui_set_drdy_hz`, etc.)               |
| `Troubleshooting/Serisl_Debug.txt`   | 136   | Final hardware serial capture (250 MHz, all tests)         |
| `Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md` | 1011 | Complete troubleshooting log (8 test runs)          |
| `Troubleshooting/two_cents.md`       | 525   | External driver design recommendation                      |
| `.cursor/plans/phase_7_ads131m02_dma_6222cc87.plan.md` | 324 | Phase 7 plan (complete)               |
| `.cursor/plans/sysclk_75_to_250_mhz_2fdbd570.plan.md`  | 204 | SYSCLK upgrade plan (complete)        |
| `.cursor/plans/snazzy-petting-mountain.md`              | 1799 | Master firmware plan                  |

---

## 13. Verified Hardware Boot Output (250 MHz, final)

```
[BOOT] DEV_ID=0x484 REV_ID=0x1007 SYSCLK=250MHz VOS0 FLASH_LAT=5
[CLK]  HCLK=250M APB1=250M APB2=250M
[CLK]  PLL1: N=31 FRAC=2048 VCO=500M P=/2 Q=/10  PLL1Q=50MHz
[CLK]  ICACHE=ON  USART1_BRR=0x10F
[SPI1] CFG1=0x10070007 MBR=/4  kernel=50MHz SCK=12.5MHz
LTC6903: init OK, word=0xCF60 (OCT=12 DAC=984 CNF=0)
LTC6903: target=8192000 Hz, computed=8191507.2 Hz
LTC6903: SPI mode switch Mode0->Mode1 complete
DIAG: TIM8 CLKIN counter started (TI1FP1/PC6)
=== LTC6903 AUTO-TRIM ===
  trimmed DAC  : 983
  measured     : 8189950 Hz
  final error  : -2050 Hz (-0.0250 %)
=== END AUTO-TRIM ===
ADS131M02: hardware reset...
ADS131M02: post-reset STATUS=0xFF22
ADS131M02: ID=0x2205
ADS131M02: init OK, ID=0x2205, STATUS=0x0103
ADS131M02: MODE=0x0111 CLOCK=0x0322 GAIN=0x0000
[USB] CDC ACTIVE after enumeration wait
[SD] 59638 MB, 4-bit bus
[FATFS] SD mounted OK
[SD] mounted OK, free=59613 MB
DIAG: TIM3 DRDY edge counter started (TI1FP1/PB4)
[DMA-TEST] OK CH0=-594 CH1=+3393
=== ADC POLARITY TEST (60 s) ===
  ... (60 DMA-TEST OK lines) ...
=== ADC POLARITY TEST SUMMARY (60 s) ===
CH0 +test: avg=+1080222 spread=4665 (30 windows)
CH0 -test: avg=-1078359 spread=4410 (30 windows)
CH1 +test: avg=+1081031 spread=4919 (30 windows)
CH1 -test: avg=-1078546 spread=4579 (30 windows)
Symmetry error: CH0=+0.1725%  CH1=+0.2299%
Total DRDY=3877727 DMA=3868686 miss=0
=== END TEST ===
=== DRDY_FMT A/B TEST (20 s) ===
[A] DRDY_FMT=0 (level), 10 s:  DRDY_SW=639731 DMA=639705 miss=0
[B] DRDY_FMT=1 (pulse), 10 s:  DRDY_SW=639748 DMA=639734 miss=0
Reverted to MODE=0x0111
=== END A/B TEST ===
ADC: sw=63980 hw=63980 ok=63979 miss=0 err=0 exti=242 dma=311
```

---

## 14. All Open Issues — NONE

All Phase 7 issues have been resolved. The following items from the previous version of this document are now closed:

| Previously Open Item            | Resolution                                                 |
|---------------------------------|-------------------------------------------------------------|
| 50% miss rate at 75 MHz         | Eliminated by SYSCLK upgrade to 250 MHz (5.48 µs margin)   |
| 25% DRDY edges lost at EXTI     | Eliminated by faster ISR execution at 250 MHz               |
| NOP guard too short at 250 MHz  | 16 NOPs = 64 ns at 250 MHz. Hardware verified: no data corruption. If ever needed, increase to ~53 NOPs or use DWT delay. |
| ICACHE disabled                 | ICACHE is ON. Verified: reduces ISR cycle counts (334→242 EXTI, 367→311 DMA). No determinism issues observed. |
| No sample ring buffer           | **Future (Phase C).** Currently only `ch0_latest`/`ch1_latest` visible to foreground. Acceptable for current use case (1 Hz telemetry + polarity test accumulators). |
| No health snapshot API          | **Future (Phase C).** `hw == sw` comparison available in main loop. |
| USB CDC disabled in main loop   | Re-enabled: `cdc_poll()`, `ui_update_fields()`, `ui_process_input()` all active. |
