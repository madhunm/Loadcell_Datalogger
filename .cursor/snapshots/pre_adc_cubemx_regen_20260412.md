# Pre-CubeMX ADC Regen Snapshot — 2026-04-12

Captured before enabling ADC1 VSENSE/VREFINT + ADC2 VDDCORE in CubeMX.
Use this to verify/restore USER CODE sections if CubeMX overwrites them.

---

## Files CubeMX will regenerate (check these after regen)

| File | Risk | Key USER CODE |
|------|------|---------------|
| `Core/Src/main.c` | **HIGH** | Includes, USER CODE 0/2/3/4, Callback 1, Error_Handler |
| `Core/Inc/main.h` | **MED** | Private defines (new CHG_PG/STAT pins expected) |
| `Core/Src/adc.c` | **HIGH** | ADC1 init + possible ADC2 new |
| `Core/Src/gpio.c` | **MED** | PB6 I2C1 block should be removed, new CHG GPIOs added |
| `Core/Src/stm32h5xx_it.c` | **HIGH** | fault_dump, EXTI2, GPDMA CH0/CH1, GPDMA CH2 (USER CODE 1) |
| `Core/Src/stm32h5xx_hal_msp.c` | LOW | Empty USER CODE blocks |
| `Core/Src/tim.c` | LOW | Empty USER CODE blocks |
| `Core/Src/spi.c` | LOW | Empty USER CODE blocks |
| `Core/Src/gpdma.c` | LOW | Empty USER CODE blocks |
| `Core/Src/usart.c` | LOW | Empty USER CODE blocks |
| `Core/Inc/stm32h5xx_hal_conf.h` | LOW | No USER CODE |
| `Debug/Core/Src/subdir.mk` | **HIGH** | Manual additions: app_state.c, battery_monitor.c |

---

## 1. Core/Src/main.c — FULL USER CODE SECTIONS

### USER CODE BEGIN Includes
```c
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
#include "battery_monitor.h"
#include "app_state.h"
#include "fatfs.h"
#include "ux_device_cdc_acm.h"
#include "ux_dcd_stm32.h"
#include <stdio.h>
#include <string.h>
```

### USER CODE BEGIN 0
```c
/**
 * @brief  Override the SDMMC1 clock divider at runtime.
 * @param[in] div  Clock divider value (10-bit, applied to CLKCR.CLKDIV).
 * @note   Used to switch from the safe init speed to 25 MHz production speed
 *         after FatFS mount succeeds.
 * @see    RM0481 §55.5.1 (SDMMC_CLKCR register)
 */
static void sdSetClkdiv(uint32_t div)
{
    MODIFY_REG(hsd1.Instance->CLKCR, SDMMC_CLKCR_CLKDIV, div & 0x3FFU);
}
```

### USER CODE BEGIN 2 (after MX inits, before while loop)
Full block from line 136 to line 301 of main.c. Key calls in order:
1. `printf("[BOOT] ...")` boot banner
2. `HAL_NVIC_DisableIRQ(EXTI2_IRQn)` suppress DRDY during init
3. `MX_USB_PCD_Init()`, `MX_SPI1_Init()`, `MX_TIM8_Init()`
4. `MX_ADC1_Init()` — Phase 9b
5. `MX_TIM2_Init()` — Phase 9a NeoPixel
6. `MX_TIM3_Init()` — Phase 7 DRDY
7. NVIC priority overrides (SPI1=1, SDMMC=5, USB=6, USART1=7, EXTI4=8, SPI2=4, TIM2=10)
8. `neoInit()` + `ledStatusInit()`
9. Chip-select housekeeping (ADC_CS, LTC_CS, IMU_CS HIGH)
10. `ltc6903Init()`, `diagClkinInit()`, `ltc6903AutoTrim()`
11. `ads131m02Init()` with LED status
12. `imuInit()` + `imuCalibrate()` with LED/UI
13. `batteryInit()` — Phase 9b
14. USB CDC init + 5s enumeration wait
15. SDMMC1 + FatFS init with LED status
16. `diagDrdyInit()`, `ads131m02StartContinuous()`
17. `ledStatusSetSys(LED_SYS_IDLE)`
18. UI draw

### USER CODE BEGIN 3 (main while loop)
Full block lines 310–484. Key sections:
- 1s UART heartbeat (`.` character)
- NeoPixel LED update at 20 Hz (`ledStatusUpdate()`)
- `ux_system_tasks_run()`, `cdcPoll()`, `diagClkinPoll()`
- `uiUpdateFields()`, `uiProcessInput()` (non-VIZ_STREAM)
- ADC stats 1 Hz block: DRDY/DMA deltas, `batteryPoll()`, UI setters, BATT printf
- VIZ_STREAM: 20 Hz $IMU CSV via CDC, 10 Hz IMU health UART report
- Normal mode: 10 Hz IMU poll, 1 Hz IMU+BATT printf

### USER CODE BEGIN Callback 1
```c
  /* TIM3 overflow: 16-bit DRDY edge counter wraps at 64 kHz (~once/second).
   * @see diag_timers.c diagDrdyTim3Overflow() */
  if (htim->Instance == TIM3)
  {
    diagDrdyTim3Overflow();
  }
```

### USER CODE BEGIN Error_Handler_Debug
```c
  /* Non-fatal: return to caller so remaining peripherals can init. */
```

---

## 2. Core/Src/stm32h5xx_it.c — CRITICAL USER CODE

### USER CODE BEGIN Includes
```c
#include "usart.h"
#include "neopixel.h"
#include <stdio.h>
#include <string.h>
```

### USER CODE BEGIN TD (fault dump helpers)
```c
static void fault_uart_puts(const char *s)
{
    HAL_UART_Transmit(&huart1, (const uint8_t *)s, (uint16_t)strlen(s), 50);
}

static void fault_uart_hex(const char *label, uint32_t val)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "  %s = 0x%08lX\r\n", label, (unsigned long)val);
    HAL_UART_Transmit(&huart1, (const uint8_t *)buf, (uint16_t)n, 50);
}

static void fault_dump(const char *name)
{
    fault_uart_puts("\r\n\r\n*** FAULT: ");
    fault_uart_puts(name);
    fault_uart_puts(" ***\r\n");
    fault_uart_hex("CFSR ", SCB->CFSR);
    fault_uart_hex("HFSR ", SCB->HFSR);
    fault_uart_hex("DFSR ", SCB->DFSR);
    fault_uart_hex("MMFAR", SCB->MMFAR);
    fault_uart_hex("BFAR ", SCB->BFAR);
    fault_uart_hex("AFSR ", SCB->AFSR);

    uint32_t cfsr = SCB->CFSR;
    if (cfsr & 0x00020000u) fault_uart_puts("  >> INVSTATE (invalid EPSR)\r\n");
    if (cfsr & 0x00010000u) fault_uart_puts("  >> UNDEFINSTR\r\n");
    if (cfsr & 0x00040000u) fault_uart_puts("  >> INVPC\r\n");
    if (cfsr & 0x00080000u) fault_uart_puts("  >> NOCP\r\n");
    if (cfsr & 0x01000000u) fault_uart_puts("  >> UNALIGNED\r\n");
    if (cfsr & 0x02000000u) fault_uart_puts("  >> DIVBYZERO\r\n");
    if (cfsr & 0x00000200u) fault_uart_puts("  >> DACCVIOL (data access)\r\n");
    if (cfsr & 0x00000100u) fault_uart_puts("  >> IACCVIOL (instr access)\r\n");
    if (cfsr & 0x00008000u) fault_uart_puts("  >> BFARVALID\r\n");
    if (cfsr & 0x00000400u) fault_uart_puts("  >> IMPRECISERR (bus)\r\n");
    if (cfsr & 0x00000800u) fault_uart_puts("  >> PRECISERR (bus)\r\n");
    if (cfsr & 0x00001000u) fault_uart_puts("  >> UNSTKERR (bus unstack)\r\n");
    if (cfsr & 0x00002000u) fault_uart_puts("  >> STKERR (bus stack)\r\n");

    fault_uart_puts("  Spinning.\r\n");
}
```

### USER CODE BEGIN HardFault_IRQn 0
```c
  fault_dump("HardFault");
```

### USER CODE BEGIN MemoryManagement_IRQn 0
```c
  fault_dump("MemManage");
```

### USER CODE BEGIN BusFault_IRQn 0
```c
  fault_dump("BusFault");
```

### USER CODE BEGIN UsageFault_IRQn 0
```c
  fault_dump("UsageFault");
```

### USER CODE BEGIN EXTI2_IRQn 0
```c
  extern void adsFastDrdyHandler(void);
  adsFastDrdyHandler();
  return;
```

### USER CODE BEGIN GPDMA1_Channel0_IRQn 0
```c
  GPDMA1_Channel0->CFCR = DMA_CFCR_TCF | DMA_CFCR_HTF | DMA_CFCR_DTEF |
                           DMA_CFCR_ULEF | DMA_CFCR_USEF | DMA_CFCR_SUSPF |
                           DMA_CFCR_TOF;
  return;
```

### USER CODE BEGIN GPDMA1_Channel1_IRQn 0
```c
  extern int adsFastDmaCompleteHandler(void);
  if (!adsFastDmaCompleteHandler())
      GPDMA1_Channel1->CFCR = DMA_CFCR_TCF | DMA_CFCR_HTF | DMA_CFCR_DTEF |
                               DMA_CFCR_ULEF | DMA_CFCR_USEF | DMA_CFCR_SUSPF |
                               DMA_CFCR_TOF;
  return;
```

### USER CODE BEGIN 1 (at end of file)
```c
void GPDMA1_Channel2_IRQHandler(void)
{
    neoDmaIrqHandler();
}
```

---

## 3. Core/Src/adc.c — FULL FILE (currently ADC1 only)

```c
ADC_HandleTypeDef hadc1;

void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }

  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }
}

// HAL_ADC_MspInit — PA1 as analog, ADC1 clock from HCLK, ADC1_IRQn prio 0
// HAL_ADC_MspDeInit — standard
```

**Note:** After regen, expect VSENSE + VREFINT channel configs added. Verify
our `battery_monitor.c` `adc1ReadChannel()` still works (it reconfigures per-call).

---

## 4. Core/Inc/main.h — GPIO DEFINES (current)

```c
#define userButton_Pin GPIO_PIN_13
#define userButton_GPIO_Port GPIOC
#define neoPixel_Pin GPIO_PIN_0
#define neoPixel_GPIO_Port GPIOA
#define battMon_Pin GPIO_PIN_1
#define battMon_GPIO_Port GPIOA
#define ADC_DRDY_Pin GPIO_PIN_2
#define ADC_DRDY_GPIO_Port GPIOA
#define ADC_DRDY_EXTI_IRQn EXTI2_IRQn
#define ADC_Reset_Pin GPIO_PIN_3
#define ADC_Reset_GPIO_Port GPIOA
#define ADC_CS_Pin GPIO_PIN_4
#define ADC_CS_GPIO_Port GPIOA
#define logStart_Pin GPIO_PIN_4
#define logStart_GPIO_Port GPIOC
#define logStart_EXTI_IRQn EXTI4_IRQn
#define LTC_CS_Pin GPIO_PIN_5
#define LTC_CS_GPIO_Port GPIOC
#define IMU_INT2_Pin GPIO_PIN_0
#define IMU_INT2_GPIO_Port GPIOB
#define USB_SENSE_Pin GPIO_PIN_1
#define USB_SENSE_GPIO_Port GPIOB
#define ledBlue_Pin GPIO_PIN_2
#define ledBlue_GPIO_Port GPIOB
#define IMU_INT1_Pin GPIO_PIN_10
#define IMU_INT1_GPIO_Port GPIOB
#define IMU_CS_Pin GPIO_PIN_12
#define IMU_CS_GPIO_Port GPIOB
#define CLKIN_Reader_Pin GPIO_PIN_6
#define CLKIN_Reader_GPIO_Port GPIOC
#define SDMMC1_Card_Detect_Pin GPIO_PIN_8
#define SDMMC1_Card_Detect_GPIO_Port GPIOA
#define DRDY_Reader_Pin GPIO_PIN_4
#define DRDY_Reader_GPIO_Port GPIOB
```

**Expected additions after regen:**
```c
#define CHG_PG_Pin GPIO_PIN_7
#define CHG_PG_GPIO_Port GPIOB
#define CHG_STAT1_Pin GPIO_PIN_5
#define CHG_STAT1_GPIO_Port GPIOB
#define CHG_STAT2_Pin GPIO_PIN_6
#define CHG_STAT2_GPIO_Port GPIOB
```

---

## 5. Core/Src/gpio.c — FULL FILE

```c
/* Current PB6 I2C1 SCL config (line 125-131) should be REMOVED after regen
   since I2C1 is being disabled.
   New CHG_PG (PB7), CHG_STAT1 (PB5), CHG_STAT2 (PB6) input configs expected. */
```

Key current GPIO configs to preserve:
- PA2 ADC_DRDY as IT_FALLING
- PA3/PA4 outputs (ADC_Reset, ADC_CS)
- PC4 logStart IT_RISING
- PC5 LTC_CS output
- PB0/PB10 IMU_INT inputs
- PB1 USB_SENSE input pull-down
- PB2 ledBlue output
- PB12 IMU_CS output
- PA8 SDMMC1_Card_Detect input
- EXTI2 prio 0, EXTI4 prio 0

---

## 6. Debug/Core/Src/subdir.mk

Manual additions that CubeMX may overwrite:
```makefile
# In C_SRCS:
../Core/Src/app_state.c \
../Core/Src/battery_monitor.c \

# Corresponding OBJS and C_DEPS entries
# And clean target entries
```

**CRITICAL:** CubeMX regenerates this file. After regen, verify `app_state.c`
and `battery_monitor.c` are still present. If missing, re-add them.

---

## 7. Non-CubeMX files (safe, not regenerated)

These files are NOT touched by CubeMX code generation:
- `Core/Inc/battery_monitor.h` / `Core/Src/battery_monitor.c`
- `Core/Inc/app_state.h` / `Core/Src/app_state.c`
- `Core/Inc/led_status.h` / `Core/Src/led_status.c`
- `Core/Inc/debug_ui.h` / `Core/Src/debug_ui.c`
- `Core/Inc/debug_uart.h` / `Core/Src/debug_uart.c`
- `Core/Inc/neopixel.h` / `Core/Src/neopixel.c`
- `Core/Inc/imu_lsm6dsv.h` / `Core/Src/imu_lsm6dsv.c`
- `Core/Inc/adc_ads131m02.h` / `Core/Src/adc_ads131m02.c`
- `Core/Inc/osc_ltc6903.h` / `Core/Src/osc_ltc6903.c`
- `Core/Inc/diag_timers.h` / `Core/Src/diag_timers.c`

---

## Post-regen verification checklist

- [ ] `main.c`: All USER CODE blocks intact (Includes, 0, 2, 3, 4, Callback 1, Error_Handler)
- [ ] `main.c`: `MX_ADC1_Init()` still in peripheral init sequence
- [ ] `main.h`: Existing GPIO defines preserved, new CHG_PG/STAT1/STAT2 added
- [ ] `adc.c`: ADC1 init has VSENSE + VREFINT channels; USER CODE blocks intact
- [ ] `gpio.c`: PB6 no longer I2C1_SCL; PB5/PB6/PB7 now charger GPIO inputs
- [ ] `stm32h5xx_it.c`: ALL fault handlers, EXTI2, GPDMA CH0/CH1/CH2 USER CODE intact
- [ ] `subdir.mk`: `app_state.c` and `battery_monitor.c` still in C_SRCS/OBJS/C_DEPS/clean
- [ ] `stm32h5xx_hal_conf.h`: No unexpected module changes
- [ ] Build succeeds with no errors
