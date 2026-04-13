# Phase 9a Snapshot — Pre-Build, Pre-CubeMX-Regen

**Date:** 2026-04-12  
**Purpose:** Preserve the exact state of all CubeMX-managed and custom code after Phase 9a implementation, before the first build and before any CubeMX IOC regeneration. Use this to restore USER CODE sections and verify CubeMX parameters if a regen overwrites them.

---

## 1. IOC Key Parameters (H562_Loadcell_Datalogger.ioc)

CubeMX does NOT know about GPDMA CH2 for NeoPixel — it was configured programmatically in `neoInit()`. These IOC parameters must remain unchanged after any regen:

```
TIM2.Channel-PWM Generation1 CH1=TIM_CHANNEL_1
TIM2.IPParameters=Channel-PWM Generation1 CH1
PA0.GPIOParameters=GPIO_Label
PA0.GPIO_Label=neoPixel
PA0.Locked=true
PA0.Signal=S_TIM2_CH1
SH.S_TIM2_CH1.0=TIM2_CH1,PWM Generation1 CH1
SH.S_TIM2_CH1.ConfNb=1
NVIC.TIM2_IRQn=true\:0\:0\:false\:false\:true\:true\:true\:true
```

Note: CubeMX sets TIM2 ARR to max (4294967295) and Prescaler to 0. Our code overrides ARR to 312 in `neoInit()`. If CubeMX ever sets a non-zero prescaler or changes the PWM channel, our driver breaks.

GPDMA CH0/CH1 (SPI1, unchanged):
```
GPDMA1.REQUEST_GPDMACH0=GPDMA1_REQUEST_SPI1_TX
GPDMA1.REQUEST_GPDMACH1=GPDMA1_REQUEST_SPI1_RX
VP_GPDMA1_VS_GPDMACH0.Mode=SIMPLEREQUEST_GPDMACH0
VP_GPDMA1_VS_GPDMACH1.Mode=SIMPLEREQUEST_GPDMACH1
```

---

## 2. USER CODE Sections in CubeMX-Managed Files

### 2.1 main.c — USER CODE BEGIN Includes

```c
/* USER CODE BEGIN Includes */
/* Uncomment to stream $IMU CSV at 20 Hz for the Python visualizer */
#define VIZ_STREAM

#include "debug_uart.h"
#include "debug_ui.h"
#include "diag_timers.h"
#include "osc_ltc6903.h"
#include "adc_ads131m02.h"
#include "imu_lsm6dsv.h"
#include "neopixel.h"
#include "led_status.h"
#include "fatfs.h"
#include "ux_device_cdc_acm.h"
#include "ux_dcd_stm32.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */
```

### 2.2 main.c — USER CODE BEGIN 2 (peripheral init sequence)

Key additions for Phase 9a:

```c
  /* MX_ADC1_Init(); */          /* Phase 9b: battery monitor */
  MX_TIM2_Init();                /* Phase 9a: NeoPixel WS2812 */
  MX_TIM3_Init();               /* Phase 7: DRDY frequency */
```

```c
  HAL_NVIC_SetPriority(TIM2_IRQn, 10, 0);  /* NeoPixel: cosmetic, lowest tier */
```

```c
  /* ── Phase 9a: NeoPixel Status LEDs ──────────────────────────── */
  if (neoInit() == 0)
    ledStatusInit();            /* LED 0 = RED HEARTBEAT (boot), LED 1 = OFF */
```

```c
  /* ── Phase 6: ADS131M02 ADC basic communication ─────────────── */
  {
    int adcRet = ads131m02Init();
    ledStatusSetSub(LED_SUB_ADC,
                    (adcRet == 0) ? LED_LEVEL_OK : LED_LEVEL_ERROR);
  }

  /* ── Phase 8: LSM6DSV IMU (SPI2, blocking, 5.0 MHz) ─────────── */
  {
    int imuRet = imuInit();
    ledStatusSetSub(LED_SUB_IMU,
                    (imuRet == 0) ? LED_LEVEL_OK : LED_LEVEL_ERROR);
  }
```

```c
          ledStatusSetSub(LED_SUB_SD, LED_LEVEL_ERROR);   /* SD card state fail */
          ledStatusSetSub(LED_SUB_SD, LED_LEVEL_ERROR);   /* FatFS fail */
      ledStatusSetSub(LED_SUB_SD, LED_LEVEL_OK);          /* SD OK */
```

```c
  ledStatusSetSys(LED_SYS_IDLE);  /* LED 0 → RED SOLID (Power ON) */
```

### 2.3 main.c — USER CODE BEGIN 3 (main loop)

```c
    /* ── NeoPixel LED status update (~20 Hz) ─────────────────────── */
    {
      static uint32_t lastLed;
      uint32_t nowLed = HAL_GetTick();
      if (nowLed - lastLed >= LED_UPDATE_INTERVAL_MS)
      {
        lastLed = nowLed;
        ledStatusUpdate();
      }
    }
```

### 2.4 stm32h5xx_it.c — USER CODE BEGIN Includes

```c
/* USER CODE BEGIN Includes */
#include "usart.h"
#include "neopixel.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */
```

### 2.5 stm32h5xx_it.c — USER CODE BEGIN 1

```c
/* USER CODE BEGIN 1 */

/**
 * @brief  GPDMA1 Channel 2 interrupt — NeoPixel DMA transfer complete.
 * @details Routes to the HAL DMA IRQ handler via neoDmaIrqHandler(), which
 *          triggers HAL_TIM_PWM_PulseFinishedCallback to stop TIM2.
 */
void GPDMA1_Channel2_IRQHandler(void)
{
    neoDmaIrqHandler();
}

/* USER CODE END 1 */
```

---

## 3. CubeMX-Generated Parameters to Verify After Regen

### tim.c — MX_TIM2_Init()

```c
htim2.Init.Prescaler = 0;            /* MUST remain 0 */
htim2.Init.Period = 4294967295;       /* Max; overridden to 312 in neoInit() */
htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
sConfigOC.OCMode = TIM_OCMODE_PWM1;  /* MUST be PWM1 */
sConfigOC.Pulse = 0;
sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;  /* MUST be HIGH */
```

### tim.c — HAL_TIM_PWM_MspInit (TIM2)

```c
__HAL_RCC_TIM2_CLK_ENABLE();
HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);  /* Overridden to 10 in main.c */
HAL_NVIC_EnableIRQ(TIM2_IRQn);
```

### tim.c — HAL_TIM_MspPostInit (TIM2)

```c
GPIO_InitStruct.Pin = neoPixel_Pin;           /* PA0 */
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;    /* MUST be AF1 for TIM2_CH1 */
```

### gpdma.c — MX_GPDMA1_Init()

Only CH0/CH1 are configured by CubeMX. CH2 is NOT configured here (we do it in neoInit()). Verify no CH2 lines appear after regen unless intentionally added.

```c
HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);
HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 0, 0);
HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);
```

---

## 4. New Files Created in Phase 9a (Not CubeMX-Managed)

These files are safe from CubeMX regen since they are purely application code:

| File | Purpose |
|------|---------|
| `Core/Inc/neopixel.h` | WS2812 driver public API |
| `Core/Src/neopixel.c` | WS2812 driver: TIM2 PWM + GPDMA CH2 |
| `Core/Inc/led_status.h` | LED status engine public API |
| `Core/Src/led_status.c` | LED status engine: priority table + rotation |

---

## 5. Build System (Debug/Core/Src/subdir.mk)

Two new entries added (auto-generated file, will be regenerated on project refresh):

```
../Core/Src/led_status.c
../Core/Src/neopixel.c
```

If STM32CubeIDE regenerates `subdir.mk`, verify these entries are present. If not, refresh the project (F5) and rebuild.
