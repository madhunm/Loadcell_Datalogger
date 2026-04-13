/**
 * @file osc_ltc6903.c
 * @brief LTC6903 programmable oscillator driver — SPI write, frequency word
 *        calculation, and DAC auto-trim.
 * @details Shares SPI1 with the ADS131M02 but requires a different SPI mode:
 *            - LTC6903  → Mode 0 (CPOL=0, CPHA=0)
 *            - ADS131M02 → Mode 1 (CPOL=0, CPHA=1)
 *
 *          The driver switches SPI1 to Mode 0 for each write and restores Mode 1
 *          afterwards.  This is safe because the driver is only called at boot,
 *          before EXTI2 is enabled, so there is no contention with the ADS DMA
 *          hot path.
 *
 *          Auto-trim uses the DWT CYCCNT-based CLKIN measurement from diag_timers
 *          to iteratively nudge the DAC toward 8,192,000 Hz.
 *
 * @author Madhu
 * @date   2026-04-12
 * @see    Datasheets/LTC6903.pdf (Linear Technology / Analog Devices)
 */

#include "osc_ltc6903.h"
#include "diag_timers.h"
#include "spi.h"
#include "main.h"
#include <stdio.h>
#include <math.h>

/* LTC6903 datasheet §Table 1 — frequency word constants.
 * f_out = 2^(10+OCT) × 2078 / (2048 − DAC)  Hz           */
#define OCT_DIVIDER   1039.0
#define OCT_RES       3.322
#define DAC_OFFSET    2048.0
#define DAC_RES       2078UL
#define DAC_OCT_OFF   10

static uint16_t g_ltcWord;       /**< Last 16-bit SPI word written. */
static uint8_t  g_oct;            /**< Current OCT field (4-bit).    */
static uint16_t g_dac;            /**< Current DAC field (10-bit).   */
static uint32_t g_measuredHz;    /**< Best CLKIN measurement (Hz).  */

/* ── Internal helpers ────────────────────────────────────────────── */

/**
 * @brief  Compute OCT and DAC from a target frequency.
 * @param[in]  freqHz  Desired output frequency in Hz.
 * @param[out] oct      Computed 4-bit OCT field.
 * @param[out] dac      Computed 10-bit DAC field.
 * @see    LTC6903 datasheet §Table 1 and §Applications Information
 */
static void computeWord(uint32_t freqHz, uint8_t *oct, uint16_t *dac)
{
    *oct = (uint8_t)(OCT_RES * log10((double)freqHz / OCT_DIVIDER));
    *dac = (uint16_t)(DAC_OFFSET -
           ((double)DAC_RES * pow(2.0, DAC_OCT_OFF + *oct) / (double)freqHz));
}

/**
 * @brief  Compute the theoretical output frequency for a given OCT/DAC pair.
 * @return Frequency in Hz (double precision).
 */
static double computedFreq(uint8_t oct, uint16_t dac)
{
    return (double)DAC_RES * pow(2.0, DAC_OCT_OFF + oct)
         / (double)(2048 - dac);
}

/**
 * @brief  Build the 16-bit frequency word, switch SPI1 to Mode 0, transmit,
 *         then restore SPI1 to Mode 1.
 * @param[in] oct  4-bit OCT field.
 * @param[in] dac  10-bit DAC field.
 * @param[in] cnf  2-bit CNF field (0 = normal oscillator output).
 * @return 0 on success, negative on SPI error.
 * @pre    ADC_CS must be HIGH (not selected).  EXTI2 disabled.
 * @post   SPI1 restored to Mode 1 for ADS131M02.
 * @note   The SPI mode switch uses HAL_SPI_Init() which also reconfigures GPIO
 *         alternate-function settings.  This is acceptable at boot but must NOT
 *         be done while the DMA hot path is running.
 */
static int ltc6903SpiWrite(uint8_t oct, uint16_t dac, uint8_t cnf)
{
    uint16_t word = ((uint16_t)(oct & 0x0F) << 12)
                  | ((uint16_t)(dac & 0x3FF) << 2)
                  | ((uint16_t)(cnf & 0x03));

    uint8_t tx[2] = { (uint8_t)(word >> 8), (uint8_t)(word & 0xFF) };

    HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_SET);

    /* Switch SPI1 to Mode 0 (CPHA = 1EDGE) for LTC6903 */
    __HAL_SPI_DISABLE(&hspi1);
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        printf("LTC6903: SPI Mode0 switch FAILED\r\n");
        return -1;
    }
    __HAL_SPI_ENABLE(&hspi1);

    HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, tx, 2, 100);
    HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_SET);

    /* Restore SPI1 to Mode 1 (CPHA = 2EDGE) for ADS131M02 */
    __HAL_SPI_DISABLE(&hspi1);
    hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        printf("LTC6903: SPI Mode1 restore FAILED\r\n");
        return -2;
    }
    __HAL_SPI_ENABLE(&hspi1);

    if (st != HAL_OK)
        return -3;

    g_ltcWord = word;
    g_oct = oct;
    g_dac = dac;
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────── */

uint16_t ltc6903GetWord(void)       { return g_ltcWord; }
uint16_t ltc6903GetDac(void)        { return g_dac; }
uint32_t ltc6903GetMeasuredHz(void) { return g_measuredHz; }

int ltc6903Init(void)
{
    uint8_t  oct;
    uint16_t dac;

    computeWord(LTC6903_TARGET_HZ, &oct, &dac);

    int rc = ltc6903SpiWrite(oct, dac, 0x00);
    if (rc < 0)
    {
        printf("LTC6903: init FAILED (rc=%d)\r\n", rc);
        return rc;
    }

    (void)computedFreq(oct, dac);

    printf("[LTC] init OK, DAC=%u, target=%lu Hz\r\n",
           dac, (unsigned long)LTC6903_TARGET_HZ);

    return 0;
}

int ltc6903SetDac(uint16_t dac)
{
    if (dac > 1023)
        dac = 1023;

    return ltc6903SpiWrite(g_oct, dac, 0x00);
}

int ltc6903AutoTrim(void)
{
    /* Hz per DAC step at current OCT (derivative of the frequency equation) */
    double stepHz = computedFreq(g_oct, g_dac)
                   / (double)(2048 - g_dac);
    double halfStep = stepHz / 2.0;

    uint16_t bestDac   = g_dac;
    uint32_t bestHz    = 0;
    int32_t  bestErr   = 0x7FFFFFFF;

    for (int iter = 0; iter < 4; iter++)
    {
        uint32_t meas = diagClkinMeasureHz(1000);
        int32_t err = (int32_t)meas - (int32_t)LTC6903_TARGET_HZ;

        if (meas > 0)
        {
            int32_t absErr = (err < 0) ? -err : err;
            int32_t absBest = (bestErr < 0) ? -bestErr : bestErr;
            if (absErr < absBest)
            {
                bestDac = g_dac;
                bestHz  = meas;
                bestErr = err;
            }
        }

        if (err > -(int32_t)halfStep && err < (int32_t)halfStep)
            break;

        /* f = K / (2048 − DAC) ⇒ higher DAC = higher freq.
         * Positive error (too high) → decrease DAC. */
        int32_t dacAdj = -(int32_t)round((double)err / stepHz);
        if (dacAdj == 0)
            dacAdj = (err > 0) ? -1 : 1;

        int32_t newDac = (int32_t)g_dac + dacAdj;
        if (newDac < 0)   newDac = 0;
        if (newDac > 1023) newDac = 1023;

        if ((uint16_t)newDac == g_dac)
            break;

        ltc6903SetDac((uint16_t)newDac);
        HAL_Delay(50);
    }

    if (g_dac != bestDac && bestHz > 0)
    {
        ltc6903SetDac(bestDac);
        HAL_Delay(50);
        bestHz = diagClkinMeasureHz(1000);
        bestErr = (int32_t)bestHz - (int32_t)LTC6903_TARGET_HZ;
    }

    g_measuredHz = bestHz;

    printf("[LTC] trim DAC=%u  error=%+ld Hz (%+.4f %%)  SPS=%.1f\r\n",
           g_dac, (long)bestErr,
           (double)bestErr / (double)LTC6903_TARGET_HZ * 100.0,
           (double)bestHz / 16384.0);

    return 0;
}
