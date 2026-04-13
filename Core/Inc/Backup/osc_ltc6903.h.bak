/**
 * @file osc_ltc6903.h
 * @brief LTC6903 programmable oscillator driver — public API.
 * @details Generates the 8.192 MHz CLKIN for the ADS131M02.  Shares SPI1 with
 *          the ADC but uses Mode 0 (CPOL=0 CPHA=0); the driver switches SPI mode
 *          internally and restores Mode 1 after each write.  Called once at boot
 *          before EXTI2 is enabled, so no contention with the DMA hot path.
 *
 *          Includes a boot-time DAC auto-trim that measures CLKIN via TIM8/DWT
 *          and nudges the DAC to minimise frequency error.
 *
 * @author Madhu
 * @date   2026-04-12
 * @see    Datasheets/LTC6903.pdf
 */

#ifndef OSC_LTC6903_H
#define OSC_LTC6903_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief Target CLKIN frequency in Hz (ADS131M02 master clock / 8 = 64 kSPS). */
#define LTC6903_TARGET_HZ  8192000UL

/**
 * @brief  Compute the LTC6903 frequency word and program the device via SPI1.
 * @return 0 on success, negative on SPI error.
 * @pre    SPI1 initialised.  EXTI2 disabled.
 * @post   LTC6903 outputs ~8.192 MHz.  SPI1 restored to Mode 1.
 */
int      ltc6903Init(void);

/**
 * @brief  Reprogram only the DAC field (keeps the current OCT).
 * @param[in] dac  10-bit DAC value (0–1023).  Clamped internally.
 * @return 0 on success, negative on SPI error.
 * @pre    ltc6903Init() completed.
 */
int      ltc6903SetDac(uint16_t dac);

/**
 * @brief  Iteratively trim the DAC so measured CLKIN is within half a DAC step
 *         of LTC6903_TARGET_HZ.
 * @return 0 on success.
 * @pre    ltc6903Init() and diagClkinInit() completed.  USB not yet up.
 * @post   g_dac and g_measuredHz updated with the best result.
 */
int      ltc6903AutoTrim(void);

/** @brief Return the last 16-bit SPI word written to the LTC6903. */
uint16_t ltc6903GetWord(void);

/** @brief Return the current DAC value (10-bit, 0–1023). */
uint16_t ltc6903GetDac(void);

/** @brief Return the most recent DWT-measured CLKIN frequency in Hz. */
uint32_t ltc6903GetMeasuredHz(void);

#ifdef __cplusplus
}
#endif

#endif /* OSC_LTC6903_H */
