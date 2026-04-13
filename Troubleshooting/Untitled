# ADS131M02 64 kSPS DMA Driver Design for STM32H562

Based on the ADS131M02 device context and the Phase 7 troubleshooting context.

## Recommendation Summary

For the ADS131M02 at 64 kSPS, I would build the driver as a **DRDY-triggered, one-frame-per-conversion SPI DMA driver**, with **PB4/TIM3_CH1 counting DRDY edges in hardware** as an independent sanity counter.

### Recommended DRDY mode

Use **pulse mode**, not level mode.

### Why pulse mode

- In **level mode**, DRDY goes low and stays low until the conversion data are shifted out, or briefly goes high before the next low transition. That makes it more stateful and easier to get confusing edge behavior while CS/SCK activity is happening.
- In **pulse mode**, each new conversion is indicated by a short negative pulse, which is much cleaner for `EXTI` on the MCU and much cleaner for **PB4 edge counting** as a truth source.
- The tradeoff is that if the host misses a DRDY pulse in pulse mode and does not read that conversion, the ADC skips a result and does not produce the next pulse until the second following ready event. That is acceptable here because the goal is to service every conversion, and PB4 then tells you immediately whether you are failing to keep up.

## ADS131M02 operating mode

Configure the ADC for:

- 24-bit words
- 64 kSPS using `CLOCK = 0x0322`
- DRDY push-pull
- DRDY pulse mode
- `DRDY_SEL = most lagging enabled channel`

Do **not** rely on a single opaque MODE-register hex literal during bring-up. Set the MODE fields symbolically so that `WLENGTH`, `DRDY_FMT`, `DRDY_HiZ`, `DRDY_SEL`, and `TIMEOUT` are explicit and reviewable.

## STM32H562 architecture

Use this MCU-side architecture:

- `SPI1` in master mode, SPI mode 1, software CS
- RX DMA + TX DMA, normal mode, 12-byte frame
- EXTI on DRDY falling edge
- PB4 / `TIM3_CH1` external clock mode to count DRDY pulses independently
- RX DMA complete interrupt only
- `SPI1_IRQn` disabled
- TX DMA IRQ disabled

## Runtime flow

### Hot path

- **EXTI DRDY ISR**
  - if not busy, assert CS
  - start one 12-byte full-duplex SPI DMA frame
- **RX DMA TC ISR**
  - wait for `TXC` or `EOT`
  - stop SPI cleanly
  - deassert CS
  - decode the 12-byte frame
  - publish one sample into a ring buffer
  - clear busy

### Foreground path

- drain the sample ring buffer
- print low-rate debug summaries over serial
- compare PB4/TIM3 hardware DRDY count against software DRDY and DMA-complete counts

## Important STM32H5 SPI rule

Do **not** treat DMA transfer-complete as the same thing as “SPI bus is fully finished”.

After RX DMA transfer complete:

- wait for `SPI_SR_TXC`
- insert a very small guard delay
- then disable SPI DMA bits
- then deassert CS
- then clear `SPE`

This is safer on STM32H5 than dropping `SPE` immediately on DMA TC.

## Driver skeleton

```c
/* ads131m02_dma.h */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ADS_FRAME_WORDS      4u
#define ADS_FRAME_BYTES      12u
#define ADS_RING_SIZE        1024u

typedef struct {
    uint32_t seq;
    uint32_t status_word;   // 24-bit word in low bits
    int32_t  ch0;
    int32_t  ch1;
    uint32_t t_cyccnt;      // optional timestamp
} ads_sample_t;

typedef struct {
    volatile uint32_t drdy_exti_count;
    volatile uint32_t drdy_hw_edges_last;
    volatile uint32_t dma_start_count;
    volatile uint32_t dma_done_count;
    volatile uint32_t dma_miss_count;
    volatile uint32_t dma_error_count;
    volatile uint32_t ring_overrun_count;
    volatile uint32_t max_exti_cycles;
    volatile uint32_t max_dma_cycles;
    volatile uint8_t  busy;
    volatile uint8_t  running;
} ads_stats_t;

bool ads_init(void);
bool ads_start_streaming(void);
void ads_stop_streaming(void);

bool ads_pop_sample(ads_sample_t *out);
const ads_stats_t *ads_get_stats(void);

/* ISR entry points */
void ads_drdy_exti_isr(void);
void ads_rx_dma_tc_isr(void);
```

```c
/* ads131m02_dma.c */

#include "ads131m02_dma.h"
#include "stm32h5xx.h"
#include <string.h>

/* --- Board-specific objects --- */
#define ADS_SPI                 SPI1
#define ADS_DMA_RX              GPDMA1_Channel1
#define ADS_DMA_TX              GPDMA1_Channel0

#define ADS_CS_LOW()            (GPIOA->BSRR = (uint32_t)GPIO_PIN_4 << 16)
#define ADS_CS_HIGH()           (GPIOA->BSRR = GPIO_PIN_4)

#define ADS_DRDY_EXTI_CLEAR()   (EXTI->FPR1 = GPIO_PIN_2)   /* adjust if your DRDY EXTI pin differs */

/* MODE-register field helpers: use symbolic fields, not one opaque literal */
#define ADS_MODE_WL_24          (1u << 8)
#define ADS_MODE_TIMEOUT_EN     (1u << 4)
#define ADS_MODE_DRDY_PULSE     (1u << 0)
#define ADS_MODE_DRDY_LEVEL     (0u << 0)
#define ADS_MODE_DRDY_PUSHPULL  (0u << 1)
#define ADS_MODE_DRDY_MOST_LAG  (0u << 2)

/* 64 kSPS @ fCLKIN = 8.192 MHz, HR + TBM */
#define ADS_CLOCK_64KSPS        0x0322u

static volatile ads_stats_t g_ads;
static volatile uint32_t g_seq;

static uint8_t ads_tx_dma[ADS_FRAME_BYTES] __attribute__((aligned(4)));
static uint8_t ads_rx_dma[ADS_FRAME_BYTES] __attribute__((aligned(4)));

static ads_sample_t ring[ADS_RING_SIZE];
static volatile uint16_t ring_wr, ring_rd;

static inline int32_t sign_extend24(uint32_t x)
{
    return (int32_t)(x << 8) >> 8;
}

static inline uint32_t be24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static bool ring_push(const ads_sample_t *s)
{
    uint16_t next = (uint16_t)((ring_wr + 1u) & (ADS_RING_SIZE - 1u));
    if (next == ring_rd) {
        g_ads.ring_overrun_count++;
        return false;
    }
    ring[ring_wr] = *s;
    ring_wr = next;
    return true;
}

bool ads_pop_sample(ads_sample_t *out)
{
    if (ring_rd == ring_wr) return false;
    *out = ring[ring_rd];
    ring_rd = (uint16_t)((ring_rd + 1u) & (ADS_RING_SIZE - 1u));
    return true;
}

const ads_stats_t *ads_get_stats(void)
{
    return &g_ads;
}

/* ---------- Low-level SPI/DMA helpers ---------- */

static void ads_spi_force_clean_idle(void)
{
    /* Disable SPI DMA paths */
    CLEAR_BIT(ADS_SPI->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);

    /* Force a SPE 1->0 transition to flush FIFOs */
    SET_BIT(ADS_SPI->CR1, SPI_CR1_SPE);
    CLEAR_BIT(ADS_SPI->CR1, SPI_CR1_SPE);

    ADS_SPI->IFCR = SPI_IFCR_ALL;
}

static void ads_dma_program_static(void)
{
    /* RX: SPI->RXDR -> memory */
    CLEAR_BIT(ADS_DMA_RX->CCR, DMA_CCR_EN);
    ADS_DMA_RX->CFCR = DMA_CFCR_ALL;
    while (!(ADS_DMA_RX->CSR & DMA_CSR_IDLEF)) {}

    ADS_DMA_RX->CSAR = (uint32_t)&ADS_SPI->RXDR;
    ADS_DMA_RX->CDAR = (uint32_t)ads_rx_dma;
    ADS_DMA_RX->CBR1 = ADS_FRAME_BYTES;
    SET_BIT(ADS_DMA_RX->CCR, DMA_CCR_TCIE);

    /* TX: memory -> SPI->TXDR */
    CLEAR_BIT(ADS_DMA_TX->CCR, DMA_CCR_EN);
    ADS_DMA_TX->CFCR = DMA_CFCR_ALL;
    while (!(ADS_DMA_TX->CSR & DMA_CSR_IDLEF)) {}

    ADS_DMA_TX->CSAR = (uint32_t)ads_tx_dma;
    ADS_DMA_TX->CDAR = (uint32_t)&ADS_SPI->TXDR;
    ADS_DMA_TX->CBR1 = ADS_FRAME_BYTES;
}

static inline void ads_spi_prepare_frame(void)
{
    ADS_SPI->IFCR = SPI_IFCR_ALL;
    MODIFY_REG(ADS_SPI->CR2, SPI_CR2_TSIZE, ADS_FRAME_BYTES);
}

static inline void ads_start_dma_frame(void)
{
    ADS_CS_LOW();

    ADS_DMA_RX->CFCR = DMA_CFCR_ALL;
    ADS_DMA_TX->CFCR = DMA_CFCR_ALL;
    ADS_DMA_RX->CBR1 = ADS_FRAME_BYTES;
    ADS_DMA_TX->CBR1 = ADS_FRAME_BYTES;

    SET_BIT(ADS_DMA_RX->CCR, DMA_CCR_EN);
    SET_BIT(ADS_DMA_TX->CCR, DMA_CCR_EN);

    ads_spi_prepare_frame();
    SET_BIT(ADS_SPI->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
    SET_BIT(ADS_SPI->CR1, SPI_CR1_SPE);
    SET_BIT(ADS_SPI->CR1, SPI_CR1_CSTART);
}

static inline void ads_finish_dma_frame(void)
{
    /* RX DMA TC does not guarantee the SPI bus is already fully done */
    while (!(ADS_SPI->SR & SPI_SR_TXC)) {}

    __DSB();
    for (volatile int i = 0; i < 16; ++i) {
        __NOP();
    }

    CLEAR_BIT(ADS_SPI->CFG1, SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
    ADS_CS_HIGH();
    CLEAR_BIT(ADS_SPI->CR1, SPI_CR1_SPE);

    ADS_SPI->IFCR = SPI_IFCR_ALL;
    ADS_DMA_RX->CFCR = DMA_CFCR_ALL;
    ADS_DMA_TX->CFCR = DMA_CFCR_ALL;
}

static void ads_publish_sample(void)
{
    ads_sample_t s;
    s.seq         = ++g_seq;
    s.status_word = be24(&ads_rx_dma[0]);
    s.ch0         = sign_extend24(be24(&ads_rx_dma[3]));
    s.ch1         = sign_extend24(be24(&ads_rx_dma[6]));
    s.t_cyccnt    = DWT->CYCCNT;

    (void)ring_push(&s);
}

/* ---------- ADC command helpers (blocking init path only) ---------- */
/* Fill these with your known-good blocking frame send/receive helpers. */

static bool ads_hw_reset_and_init_registers(void)
{
    /*
      1) HW reset
      2) NULL, read STATUS/ID
      3) Write MODE:
         - 24-bit
         - push-pull DRDY
         - pulse DRDY
         - most-lagging channel select
         - keep TIMEOUT enabled during development unless you have a reason not to
      4) Write CLOCK = 0x0322
      5) Write GAIN1 = 0x0000
      6) Read back registers
    */
    return true;
}

/* ---------- Public API ---------- */

bool ads_init(void)
{
    memset((void *)&g_ads, 0, sizeof(g_ads));
    memset(ads_tx_dma, 0, sizeof(ads_tx_dma));
    ring_wr = ring_rd = 0;
    g_seq = 0;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if (!ads_hw_reset_and_init_registers()) {
        return false;
    }

    ads_spi_force_clean_idle();
    ads_dma_program_static();

    /* Only RX DMA completion interrupt is needed */
    NVIC_DisableIRQ(SPI1_IRQn);
    NVIC_DisableIRQ(GPDMA1_Channel0_IRQn);
    NVIC_ClearPendingIRQ(SPI1_IRQn);
    NVIC_ClearPendingIRQ(GPDMA1_Channel0_IRQn);
    NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn);

    return true;
}

bool ads_start_streaming(void)
{
    g_ads.busy = 0;
    g_ads.running = 1;

    ads_spi_force_clean_idle();
    ads_dma_program_static();

    NVIC_ClearPendingIRQ(GPDMA1_Channel1_IRQn);
    NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);

    /* enable EXTI last */
    NVIC_ClearPendingIRQ(EXTI2_IRQn);
    NVIC_EnableIRQ(EXTI2_IRQn);

    return true;
}

void ads_stop_streaming(void)
{
    NVIC_DisableIRQ(EXTI2_IRQn);
    NVIC_DisableIRQ(GPDMA1_Channel1_IRQn);

    g_ads.running = 0;

    CLEAR_BIT(ADS_DMA_RX->CCR, DMA_CCR_EN);
    CLEAR_BIT(ADS_DMA_TX->CCR, DMA_CCR_EN);
    while (!(ADS_DMA_RX->CSR & DMA_CSR_IDLEF)) {}
    while (!(ADS_DMA_TX->CSR & DMA_CSR_IDLEF)) {}

    ads_spi_force_clean_idle();
    ADS_CS_HIGH();
    g_ads.busy = 0;
}

/* ---------- ISRs ---------- */

void ads_drdy_exti_isr(void)
{
    uint32_t t0 = DWT->CYCCNT;

    ADS_DRDY_EXTI_CLEAR();
    g_ads.drdy_exti_count++;

    if (!g_ads.running) return;

    if (g_ads.busy) {
        g_ads.dma_miss_count++;
        return;
    }

    g_ads.busy = 1;
    g_ads.dma_start_count++;
    ads_start_dma_frame();

    uint32_t dt = DWT->CYCCNT - t0;
    if (dt > g_ads.max_exti_cycles) g_ads.max_exti_cycles = dt;
}

void ads_rx_dma_tc_isr(void)
{
    uint32_t t0 = DWT->CYCCNT;

    if (ADS_DMA_RX->CSR & DMA_CSR_TCF) {
        ads_finish_dma_frame();
        ads_publish_sample();
        g_ads.dma_done_count++;
        g_ads.busy = 0;
    } else {
        ADS_DMA_RX->CFCR = DMA_CFCR_ALL;
        ADS_DMA_TX->CFCR = DMA_CFCR_ALL;
        g_ads.dma_error_count++;
        g_ads.busy = 0;
    }

    uint32_t dt = DWT->CYCCNT - t0;
    if (dt > g_ads.max_dma_cycles) g_ads.max_dma_cycles = dt;
}
```

## PB4 / TIM3 sanity-check path

Do **not** use PB4 to control sampling.

Use PB4 only as an independent observer.

The sampling truth path should stay:

- `DRDY EXTI -> SPI DMA`

PB4 should answer only this:

- “Did the ADC really generate as many DRDY events as the software thinks it handled?”

Example hardware sanity snapshot:

```c
typedef struct {
    uint32_t hw_drdy_total;
    uint32_t sw_drdy_total;
    uint32_t dma_done_total;
    uint32_t dma_miss_total;
} ads_health_t;

void ads_health_snapshot(ads_health_t *h)
{
    uint32_t tim_cnt = TIM3->CNT;  // PB4 external clock count

    h->hw_drdy_total  = tim_cnt;
    h->sw_drdy_total  = g_ads.drdy_exti_count;
    h->dma_done_total = g_ads.dma_done_count;
    h->dma_miss_total = g_ads.dma_miss_count;
}
```

## Serial debug during development

Yes, you can and should use serial during development, but only in a controlled way.

### Safe debug strategy

Use serial only from the **foreground** at a **low rate**, for example every 100–500 ms.

Example:

```c
void ads_debug_telemetry_task(void)
{
    static uint32_t last_ms;
    if ((millis() - last_ms) < 250) return;
    last_ms = millis();

    ads_health_t h;
    ads_health_snapshot(&h);

    printf("ADS hw=%lu sw=%lu ok=%lu miss=%lu busy=%u maxExti=%lu maxDma=%lu\r\n",
           h.hw_drdy_total,
           h.sw_drdy_total,
           h.dma_done_total,
           h.dma_miss_total,
           g_ads.busy,
           g_ads.max_exti_cycles,
           g_ads.max_dma_cycles);
}
```

### Do not do this

Do **not** print from:

- EXTI ISR
- DMA complete ISR
- SPI hot path
- per-sample callbacks

At 64 kSPS, that will distort timing and can easily break the very driver you are trying to debug.

### Good development telemetry

Use:

- low-rate `printf` summaries
- RAM counters
- GPIO pulses for timing inspection
- DWT cycle counts
- ITM/SWO or RTT if available

## Final recommendation

### Use this design

- **DRDY mode:** pulse mode
- **Triggering:** EXTI falling edge
- **Sampling engine:** one 12-byte full-duplex SPI DMA transaction per DRDY
- **Sanity check:** PB4/TIM3 hardware DRDY edge counter, compared periodically with software counters
- **Serial during development:** yes, but only as low-rate foreground telemetry, never in the hot path

### Start simple

Start in:

- 24-bit word mode
- one DMA frame per DRDY
- no per-sample logging
- PB4 edge counter enabled from the beginning

That gives the cleanest path to validating true 64 kSPS operation.
