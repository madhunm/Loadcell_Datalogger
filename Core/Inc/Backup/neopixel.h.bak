/**
 * @file    neopixel.h
 * @brief   Low-level WS2812 NeoPixel driver — public API.
 * @details TIM2 CH1 PWM + GPDMA one-shot transfer for 2 daisy-chained WS2812
 *          LEDs on PA0.  Upstream: TIM2 HAL, GPDMA CH2.
 *          Downstream: led_status.c (high-level pattern engine).
 * @author  Madhu
 * @date    2026-04-12
 */

#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define NEO_LED_COUNT       2u                                /**< Daisy-chained WS2812 LEDs on PA0      */
#define NEO_BITS_PER_LED    24u                               /**< 8 bits x 3 colours (GRB)              */
#define NEO_DMA_BUF_SIZE    (NEO_LED_COUNT * NEO_BITS_PER_LED + 1u) /**< +1 trailing zero for output-low */

/**
 * @brief  Initialise TIM2 PWM + DMA for WS2812 output on PA0.
 * @details Configures TIM2 ARR for 800 kHz bit rate, enables OC1 preload,
 *          and sets up GPDMA1 CH2 for memory-to-CCR1 transfers.
 *          All LEDs set to OFF after init.
 * @return  0 on success, -1 on DMA configuration failure.
 * @pre     MX_TIM2_Init() must have been called.
 * @post    TIM2 stopped.  DMA ready for neoShow() trigger.
 * @see     WS2812B datasheet timing table.
 */
int neoInit(void);

/**
 * @brief  Set one pixel's colour in the DMA buffer (not yet transmitted).
 * @param[in] idx  Pixel index (0 = LED 0 system, 1 = LED 1 subsystem).
 * @param[in] r    Red intensity 0-255.
 * @param[in] g    Green intensity 0-255.
 * @param[in] b    Blue intensity 0-255.
 * @note   WS2812 expects GRB byte order; this function handles the reorder.
 */
void neoSetPixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  Trigger one-shot DMA transfer to push the pixel buffer to the LEDs.
 * @details Starts TIM2 PWM + DMA; DMA TC callback stops TIM2 automatically.
 *          Returns immediately — transfer completes in ~60 us for 2 LEDs.
 * @pre    neoInit() must have succeeded.
 */
void neoShow(void);

/**
 * @brief  Turn all LEDs off immediately.
 */
void neoOff(void);

/**
 * @brief  GPDMA1 Channel 2 IRQ forwarding — call from GPDMA1_Channel2_IRQHandler.
 * @details Routes the DMA interrupt to the HAL DMA IRQ handler for the NeoPixel
 *          DMA channel, which in turn triggers HAL_TIM_PWM_PulseFinishedCallback.
 */
void neoDmaIrqHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* NEOPIXEL_H */
