/**
 * @file adc_ads131m02.h
 * @brief ADS131M02 2-channel delta-sigma ADC driver — register map and public API.
 * @details Provides blocking register access (Phase 6) and DRDY-triggered DMA
 *          continuous capture at 64 kSPS (Phase 7).  SPI1 Mode 1 (CPOL=0,
 *          CPHA=1), 24-bit word mode, 12-byte frames.
 *
 *          Upstream:   DRDY falling edge on PA2 (EXTI2).
 *          Downstream: adsDmaStats_t consumed by main loop for UI / logging.
 *
 * @author Madhu
 * @date   2026-04-12
 * @see    Datasheets/ads131m02.pdf  (TI SBAS853A)
 * @see    References/ADS131M02_CONTEXT.md
 */

#ifndef ADC_ADS131M02_H
#define ADC_ADS131M02_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Register addresses (ADS131M02 datasheet Table 18) ─────────── */
#define ADS_REG_ID              0x00
#define ADS_REG_STATUS          0x01
#define ADS_REG_MODE            0x02
#define ADS_REG_CLOCK           0x03
#define ADS_REG_GAIN1           0x04
#define ADS_REG_CFG             0x06
#define ADS_REG_THRSHLD_MSB     0x07
#define ADS_REG_THRSHLD_LSB     0x08
#define ADS_REG_CH0_CFG         0x09
#define ADS_REG_CH0_OCAL_MSB    0x0A
#define ADS_REG_CH0_OCAL_LSB    0x0B
#define ADS_REG_CH0_GCAL_MSB    0x0C
#define ADS_REG_CH0_GCAL_LSB    0x0D
#define ADS_REG_CH1_CFG         0x0E
#define ADS_REG_CH1_OCAL_MSB    0x0F
#define ADS_REG_CH1_OCAL_LSB    0x10
#define ADS_REG_CH1_GCAL_MSB    0x11
#define ADS_REG_CH1_GCAL_LSB    0x12
#define ADS_REG_REGMAP_CRC      0x3E

/* ── Command opcodes (16-bit, MSB-aligned in 24-bit word) ──────── */
#define ADS_CMD_NULL            0x0000
#define ADS_CMD_RESET           0x0011
#define ADS_CMD_STANDBY         0x0022
#define ADS_CMD_WAKEUP          0x0033
#define ADS_CMD_LOCK            0x0555
#define ADS_CMD_UNLOCK          0x0655
#define ADS_CMD_RREG            0xA000
#define ADS_CMD_WREG            0x6000

/**
 * @brief Build a RREG command for the given register address.
 * @see   ADS131M02 datasheet §9.5.2.6, address encoding: 101a_aaaa_annn_nnnn
 */
#define ADS_RREG(addr)          (ADS_CMD_RREG | ((uint16_t)(addr) << 7))

/**
 * @brief Build a WREG command for the given register address.
 * @see   ADS131M02 datasheet §9.5.2.7, address encoding: 011a_aaaa_annn_nnnn
 */
#define ADS_WREG(addr)          (ADS_CMD_WREG | ((uint16_t)(addr) << 7))

/* ── STATUS register bit masks ──────────────────────────────────── */
#define ADS_STATUS_LOCK         (1u << 15)
#define ADS_STATUS_F_RESYNC     (1u << 14)
#define ADS_STATUS_REG_MAP      (1u << 13)
#define ADS_STATUS_CRC_ERR      (1u << 12)
#define ADS_STATUS_RESET        (1u << 10)
#define ADS_STATUS_DRDY1        (1u <<  1)
#define ADS_STATUS_DRDY0        (1u <<  0)

/* ── ID register ────────────────────────────────────────────────── */
#define ADS_ID_M02_MASK         0xFF00u   /**< Upper byte identifies the device variant. */
#define ADS_ID_M02_EXPECTED     0x2200u   /**< ADS131M02: prefix=0010, CHANCNT=0010.    */

/* ── MODE register (addr 0x02, reset = 0x0510) ─────────────────── */
#define ADS_MODE_DEFAULT        0x0510u
#define ADS_MODE_DRDY_LEVEL     0x0000u   /**< DRDY_FMT=0: level until read.            */
#define ADS_MODE_DRDY_PULSE     0x0001u   /**< DRDY_FMT=1: fixed-width pulse.           */
#define ADS_MODE_REG_CRC_DIS    0x0000u
#define ADS_MODE_RX_CRC_DIS     0x0000u
#define ADS_MODE_CRC_TYPE_CCITT 0x0000u
#define ADS_MODE_WLENGTH_24     0x0100u   /**< WLENGTH=01: 24-bit words.                */
#define ADS_MODE_TIMEOUT_EN     0x0010u   /**< SPI TIMEOUT enabled.                     */
#define ADS_MODE_RESET_CLR      0x0000u

/** @brief Combined MODE value written during init. */
#define ADS_MODE_INIT    (ADS_MODE_WLENGTH_24 | ADS_MODE_TIMEOUT_EN | \
                          ADS_MODE_DRDY_PULSE)

/* ── CLOCK register (addr 0x03, reset = 0x030E) ───────────────── */
/** @brief CLOCK value for 64 kSPS: CH0+CH1 enabled, TBM=1, HR mode. @see Datasheet Table 25. */
#define ADS_CLOCK_64KSPS        0x0322u

/* ── GAIN1 register ─────────────────────────────────────────────── */
#define ADS_GAIN1_1X            0x0000u   /**< PGA gain = 1× on both channels. */

/* ── Frame geometry (24-bit word mode, M02 = 2 channels) ────────── */
#define ADS_WORD_BYTES          3
#define ADS_NUM_WORDS           4         /**< CMD/STATUS + CH0 + CH1 + CRC. */
#define ADS_FRAME_BYTES         (ADS_WORD_BYTES * ADS_NUM_WORDS)  /**< 12 bytes per frame. */

/* ── CHn_CFG MUX bits (datasheet Table 37) ───────────────────────── */
#define ADS_MUX_EXTERNAL        0x0000u   /**< Normal analog inputs.             */
#define ADS_MUX_AGND            0x0001u   /**< Both inputs shorted to AGND.      */
#define ADS_MUX_POS_TEST        0x0002u   /**< Positive DC test signal.          */
#define ADS_MUX_NEG_TEST        0x0003u   /**< Negative DC test signal.          */

/* ── DMA continuous-capture statistics ──────────────────────────── */

/**
 * @brief Runtime statistics for the DRDY-triggered DMA capture pipeline.
 * @details Updated from ISR context; all members are volatile.  The main loop
 *          reads a snapshot via ads131m02GetStats() without disabling IRQs
 *          (individual 32-bit reads are atomic on Cortex-M33).
 */
typedef struct {
    volatile uint32_t drdyCount;       /**< Total DRDY EXTI edges since start.         */
    volatile uint32_t dmaCount;        /**< Completed DMA transfers (valid samples).    */
    volatile uint32_t dmaStartCount;  /**< DMA transfers initiated (includes skipped). */
    volatile uint32_t missCount;       /**< DRDYs missed because DMA was still busy.    */
    volatile uint32_t dmaErrorCount;  /**< DMA completions without TCF (error path).   */
    volatile int32_t  ch0Latest;       /**< Most recent CH0 sample (sign-extended 24→32). */
    volatile int32_t  ch1Latest;       /**< Most recent CH1 sample (sign-extended 24→32). */

    volatile uint32_t maxExtiCycles;  /**< Worst-case DWT cycle count in EXTI2 ISR.    */
    volatile uint32_t maxDmaCycles;   /**< Worst-case DWT cycle count in DMA-complete.  */

    /* Per-window accumulator for polarity self-test (ISR-updated). */
    volatile int32_t  ch0Min;          /**< CH0 minimum during accumulation window. */
    volatile int32_t  ch0Max;          /**< CH0 maximum during accumulation window. */
    volatile int64_t  ch0Sum;          /**< CH0 running sum for average.            */
    volatile int32_t  ch1Min;          /**< CH1 minimum during accumulation window. */
    volatile int32_t  ch1Max;          /**< CH1 maximum during accumulation window. */
    volatile int64_t  ch1Sum;          /**< CH1 running sum for average.            */
    volatile uint32_t windowCount;     /**< Samples accumulated in current window.  */
    volatile uint8_t  accumulate;       /**< 1 = ISR updates min/max/sum; 0 = frozen. */
} adsDmaStats_t;

/* ── Public API — Phase 6 (blocking register access) ───────────── */

/**
 * @brief  Initialise the ADS131M02: hardware reset, register configuration, verify.
 * @return 0 on success, negative error code on failure.
 * @pre    SPI1 initialised (Mode 1, 12.5 MHz).  EXTI2 must be disabled.
 * @post   MODE, CLOCK, GAIN1 registers written and verified.  Device streaming
 *         at 64 kSPS but DRDY edges are not yet serviced (EXTI2 still off).
 * @see    ADS131M02 datasheet §9.5.4 (Power-Up Sequence)
 */
int      ads131m02Init(void);

/**
 * @brief  Apply PGA gains from calibration (after ads131m02Init, before streaming).
 * @details Writes GAIN1 (reg 0x04). CH0 and CH1 gains are powers of two in 1..128
 *          (64 unsupported). Init leaves 1×/1×; this applies factory values.
 * @param[in] ch0Gain  CH0 (bridge) PGA gain as float (1, 2, 4, …, 128).
 * @param[in] ch1Gain  CH1 (excitation sense) PGA gain — typically 1.0.
 * @pre    EXTI2 disabled; not in continuous DMA mode.
 * @see    ADS131M02 datasheet Table 16 (GAIN1 register).
 */
void     ads131m02SetGain(float ch0Gain, float ch1Gain);

/**
 * @brief  Read a single 16-bit register (two-frame pipelined read).
 * @param[in] addr  Register address (0x00–0x3E).
 * @return 16-bit register value.
 * @pre    EXTI2 disabled (blocking SPI1 access).
 */
uint16_t ads131m02ReadReg(uint8_t addr);

/**
 * @brief  Write a 16-bit register with read-back verification.
 * @param[in] addr  Register address.
 * @param[in] val   Value to write.
 * @return 0 on success, -1 if read-back does not match.
 * @pre    EXTI2 disabled.
 */
int      ads131m02WriteReg(uint8_t addr, uint16_t val);

/**
 * @brief  Print all key registers to the debug console.
 * @pre    EXTI2 disabled.
 */
void     ads131m02DumpRegs(void);

/* ── Public API — Phase 7 (DMA continuous capture) ─────────────── */

/**
 * @brief  Begin DRDY-triggered DMA capture at 64 kSPS.
 * @details Runs a single-frame manual DMA test, then arms EXTI2 so each
 *          DRDY falling edge triggers a register-level DMA read.
 * @pre    ads131m02Init() completed.  EXTI2 may be enabled or disabled.
 * @post   EXTI2 enabled; ISR handles every DRDY edge via adsFastDrdyHandler().
 */
void     ads131m02StartContinuous(void);

/**
 * @brief  Stop DMA capture and return SPI1/GPDMA to idle.
 * @post   EXTI2 remains enabled but the ISR no-ops (adsDmaStop == 1).
 *         HAL SPI/DMA handles reset to READY state for any future blocking use.
 */
void     ads131m02StopContinuous(void);

/**
 * @brief  Get a pointer to the live DMA statistics structure.
 * @return Pointer to the volatile stats (do not cache; re-read each access).
 */
const volatile adsDmaStats_t *ads131m02GetStats(void);

/**
 * @brief  Run an alternating-polarity self-test over @p durationS seconds.
 * @param[in] durationS  Test duration (alternates +/- MUX each second).
 * @pre    Continuous capture running.
 * @post   MUX restored to ADS_MUX_EXTERNAL; continuous capture re-started.
 */
void     ads131m02PolarityTest(uint32_t durationS);

/**
 * @brief  Run an A/B comparison of DRDY level mode vs pulse mode.
 * @param[in] armSeconds  Duration per arm (total test = 2× armSeconds).
 * @post   MODE register restored to ADS_MODE_INIT; continuous capture re-started.
 */
void     ads131m02DrdyFmtTest(uint32_t armSeconds);

/* ── Fast-path ISR entry points (called from stm32h5xx_it.c USER CODE) ── */

/**
 * @brief  EXTI2 ISR handler — count DRDY, start DMA if idle.
 * @note   Clears the EXTI pending flag directly (bypasses HAL for speed).
 *         Uses DWT CYCCNT to track worst-case cycle count.
 * @pre    Called from EXTI2_IRQHandler() in stm32h5xx_it.c, priority 0.
 */
void     adsFastDrdyHandler(void);

/**
 * @brief  GPDMA1_CH1 RX-complete ISR handler — extract sample, deassert CS.
 * @return 1 if flags were handled, 0 if the caller should clear DMA flags.
 * @note   Waits for SPI TXC before CS deassert to avoid truncating the bus frame.
 *         Guard NOPs provide the tCSH hold time required by the ADS131M02.
 * @pre    Called from GPDMA1_Channel1_IRQHandler(), priority 0.
 * @see    RM0481 §44.4.15 (SPI TXC flag), ADS131M02 datasheet §7.5 (SPI timing)
 */
int      adsFastDmaCompleteHandler(void);

/**
 * @brief  Sign-extend a 24-bit two's-complement value to 32 bits.
 * @param[in] raw24  Unsigned 24-bit sample from the ADC frame.
 * @return Signed 32-bit value.
 */
static inline int32_t adsSignExtend24(uint32_t raw24)
{
    return (int32_t)(raw24 << 8) >> 8;
}

#ifdef __cplusplus
}
#endif

#endif /* ADC_ADS131M02_H */
