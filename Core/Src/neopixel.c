/**
 * @file    neopixel.c
 * @brief   Low-level WS2812 NeoPixel driver — TIM2 PWM + DMA implementation.
 * @details Encodes RGB values into a DMA buffer of TIM2 CCR duty-cycle values
 *          (GRB byte order, MSB first).  neoShow() triggers a one-shot GPDMA
 *          transfer; DMA TC callback stops TIM2 until next neoShow().
 * @author  Madhu
 * @date    2026-04-12
 * @see     WS2812B datasheet (800 kHz, T0H=0.4 us, T1H=0.8 us).
 */

#include "neopixel.h"
#include "tim.h"
#include "stm32h5xx_hal.h"
#include <string.h>
#include <stdio.h>

/* WS2812B bit timing at 250 MHz TIM2 clock (prescaler = 0).
 * Each bit period is 1.25 us = 312.5 counts, rounded to 312. */
#define NEO_TIM_PERIOD      312u    /**< ARR value: 250 MHz / 800 kHz        */
#define NEO_BIT0_DUTY       100u    /**< CCR for logic 0: ~0.4 us high       */
#define NEO_BIT1_DUTY       200u    /**< CCR for logic 1: ~0.8 us high       */

static DMA_HandleTypeDef hdmaNeo;
static uint32_t dmaBuf[NEO_DMA_BUF_SIZE];
static volatile uint8_t neoBusy;

/* ── HAL callback — DMA transfer complete ─────────────────────── */

/**
 * @brief  HAL weak-override: stop TIM2 PWM after the NeoPixel DMA transfer.
 * @param[in] htim  Timer handle — only TIM2 is handled here.
 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_1);
        neoBusy = 0;
    }
}

/* ── Public API ───────────────────────────────────────────────── */

int neoInit(void)
{
    /* Override ARR for 800 kHz bit rate (CubeMX sets max 32-bit value) */
    __HAL_TIM_SET_AUTORELOAD(&htim2, NEO_TIM_PERIOD);

    /* Enable OC1 preload so DMA writes take effect at the next update event,
     * preventing mid-period CCR glitches.  See RM0481 §33.4.9. */
    htim2.Instance->CCMR1 |= TIM_CCMR1_OC1PE;

    /* ── Configure GPDMA1 Channel 2 for TIM2 CH1 CC DMA ──────── */
    hdmaNeo.Instance                   = GPDMA1_Channel2;
    hdmaNeo.Init.Request               = GPDMA1_REQUEST_TIM2_CH1;
    hdmaNeo.Init.BlkHWRequest          = DMA_BREQ_SINGLE_BURST;
    hdmaNeo.Init.Direction             = DMA_MEMORY_TO_PERIPH;
    hdmaNeo.Init.SrcInc                = DMA_SINC_INCREMENTED;
    hdmaNeo.Init.DestInc               = DMA_DINC_FIXED;
    hdmaNeo.Init.SrcDataWidth          = DMA_SRC_DATAWIDTH_WORD;
    hdmaNeo.Init.DestDataWidth         = DMA_DEST_DATAWIDTH_WORD;
    hdmaNeo.Init.Priority              = DMA_LOW_PRIORITY_LOW_WEIGHT;
    hdmaNeo.Init.SrcBurstLength        = 1;
    hdmaNeo.Init.DestBurstLength       = 1;
    hdmaNeo.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0
                                       | DMA_DEST_ALLOCATED_PORT0;
    hdmaNeo.Init.TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER;
    hdmaNeo.Init.Mode                  = DMA_NORMAL;

    if (HAL_DMA_Init(&hdmaNeo) != HAL_OK)
    {
        printf("[NEO] DMA init FAILED\r\n");
        return -1;
    }

    /* Link DMA handle to TIM2 CC1 slot */
    __HAL_LINKDMA(&htim2, hdma[TIM_DMA_ID_CC1], hdmaNeo);

    /* NVIC for NeoPixel DMA — low priority, must never preempt ADC hot path */
    HAL_NVIC_SetPriority(GPDMA1_Channel2_IRQn, 10, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel2_IRQn);

    memset(dmaBuf, 0, sizeof(dmaBuf));
    neoBusy = 0;

    printf("[NEO] init OK, %u LEDs on PA0, DMA CH2\r\n",
           (unsigned)NEO_LED_COUNT);
    return 0;
}

void neoSetPixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx >= NEO_LED_COUNT)
        return;

    /* WS2812 expects GRB byte order, MSB first */
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
    uint32_t *p  = &dmaBuf[idx * NEO_BITS_PER_LED];

    for (int bit = 23; bit >= 0; bit--)
        *p++ = (grb & (1u << bit)) ? NEO_BIT1_DUTY : NEO_BIT0_DUTY;
}

void neoShow(void)
{
    if (neoBusy)
        return;
    neoBusy = 1;

    /* Trailing zero forces output LOW for the final period */
    dmaBuf[NEO_DMA_BUF_SIZE - 1] = 0;

    /* Pre-load first CCR value and force preload → active via software UG.
     * This ensures the very first PWM period has the correct duty cycle.
     * See RM0481 §33.4.3 — UG bit resets counter and transfers shadow regs. */
    htim2.Instance->CCR1 = dmaBuf[0];
    htim2.Instance->EGR  = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

    /* Start PWM + DMA from the second element (first is already in CCR1) */
    HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_1,
                           &dmaBuf[1], NEO_DMA_BUF_SIZE - 1);
}

void neoOff(void)
{
    neoSetPixel(0, 0, 0, 0);
    neoSetPixel(1, 0, 0, 0);
    neoShow();
}

void neoDmaIrqHandler(void)
{
    HAL_DMA_IRQHandler(&hdmaNeo);
}
