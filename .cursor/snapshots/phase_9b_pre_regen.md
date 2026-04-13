# Phase 9b Pre-Regen Snapshot

Captured after all Phase 9b code is written, before CubeMX IOC regeneration.
Use this to verify USER CODE sections survive the regen.

## USER CODE sections in main.c (must survive regen)

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

### USER CODE BEGIN 2 (key additions)
- `MX_ADC1_Init();` — uncommented
- `MX_TIM2_Init();` — was already uncommented in Phase 9a
- NVIC priority overrides block
- `if (neoInit() == 0) ledStatusInit();`
- Chip-select housekeeping
- LTC6903, TIM8, TIM3 inits
- `ads131m02Init()` with LED status
- `imuInit()` + calibrate with LED status
- `batteryInit();` — Phase 9b
- USB CDC init + enumeration wait
- SDMMC + FatFS init with LED status
- `diagDrdyInit()`, `ads131m02StartContinuous()`
- `ledStatusSetSys(LED_SYS_IDLE);`
- UI draw

### USER CODE BEGIN 3 (main loop, key additions)
- 1s UART heartbeat
- NeoPixel LED status update (20 Hz)
- `ux_system_tasks_run()`, `cdcPoll()`, `diagClkinPoll()`
- ADC stats 1 Hz block with `batteryPoll()` + UI setters (Phase 9b)
- VIZ_STREAM or normal IMU mode

## USER CODE sections in stm32h5xx_it.c (must survive regen)

### USER CODE BEGIN Includes
```c
#include "neopixel.h"
```

### USER CODE BEGIN 1
```c
void GPDMA1_Channel2_IRQHandler(void)
{
    neoDmaIrqHandler();
}
```

## New application files (not affected by regen)
- `Core/Inc/battery_monitor.h`
- `Core/Src/battery_monitor.c`
- `Core/Inc/app_state.h`
- `Core/Src/app_state.c`

## CubeMX-managed files to verify after regen

### main.h — expected NEW defines after regen
```
CHG_PG_Pin        GPIO_PIN_7
CHG_PG_GPIO_Port  GPIOB
CHG_STAT1_Pin     GPIO_PIN_5
CHG_STAT1_GPIO_Port GPIOB
CHG_STAT2_Pin     GPIO_PIN_6
CHG_STAT2_GPIO_Port GPIOB
```

### gpio.c — expected changes
- PB7 init as input with pull-up (CHG_PG)
- PB5 init as input, no pull (CHG_STAT1)
- PB6 init as input, no pull (CHG_STAT2)
- I2C1 SCL/SDA GPIO init removed

### adc.c — expected changes
- VSENSE (temperature sensor) channel may appear in init
- PA1/CH1 config preserved

### Files to verify unchanged
- `tim.c` — TIM2/TIM3/TIM8 configs
- `gpdma.c` — GPDMA CH0/CH1 configs
- `spi.c` — SPI1/SPI2 configs
- `sdmmc.c` — SDMMC1 config
