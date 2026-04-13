/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h5xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h5xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include "neopixel.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

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

/**
 * @brief  Dump SCB fault registers over UART and spin.
 *         Uses only HAL_UART_Transmit (no printf / CDC / malloc).
 */
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

    /* Decode CFSR sub-fields for quick reading */
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

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern SPI_HandleTypeDef hspi2;
extern ADC_HandleTypeDef hadc1;
extern CORDIC_HandleTypeDef hcordic;
extern FMAC_HandleTypeDef hfmac;
extern SD_HandleTypeDef hsd1;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern UART_HandleTypeDef huart1;
extern PCD_HandleTypeDef hpcd_USB_DRD_FS;
extern TIM_HandleTypeDef htim6;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  fault_dump("HardFault");
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  fault_dump("MemManage");
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  fault_dump("BusFault");
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  fault_dump("UsageFault");
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */

  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32H5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h5xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI Line2 interrupt.
  */
void EXTI2_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI2_IRQn 0 */
  /* ADC DRDY falling edge → register-level DMA read (bypass HAL for speed).
   * @see adc_ads131m02.c adsFastDrdyHandler()
   * @see RM0481 §14.3 (GPDMA), ADS131M02 datasheet §7.4 (DRDY timing) */
  extern void adsFastDrdyHandler(void);
  adsFastDrdyHandler();
  return;
  /* USER CODE END EXTI2_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(ADC_DRDY_Pin);
  /* USER CODE BEGIN EXTI2_IRQn 1 */

  /* USER CODE END EXTI2_IRQn 1 */
}

/**
  * @brief This function handles EXTI Line4 interrupt.
  */
void EXTI4_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI4_IRQn 0 */

  /* USER CODE END EXTI4_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(logStart_Pin);
  /* USER CODE BEGIN EXTI4_IRQn 1 */

  /* USER CODE END EXTI4_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 0 global interrupt.
  */
void GPDMA1_Channel0_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 0 */
  /* SPI1 TX DMA complete — unconditionally clear all flags and return.
   * TX-complete is not needed by the register-level fast path; preventing
   * HAL_DMA_IRQHandler from processing stale HAL state avoids HardFault.
   * @see Troubleshooting/PHASE7_DMA_CRASH_CONTEXT.md §17 */
  GPDMA1_Channel0->CFCR = DMA_CFCR_TCF | DMA_CFCR_HTF | DMA_CFCR_DTEF |
                           DMA_CFCR_ULEF | DMA_CFCR_USEF | DMA_CFCR_SUSPF |
                           DMA_CFCR_TOF;
  return;
  /* USER CODE END GPDMA1_Channel0_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 1 */

  /* USER CODE END GPDMA1_Channel0_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 1 global interrupt.
  */
void GPDMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 0 */
  /* SPI1 RX DMA complete — extract ADC sample data and deassert CS.
   * If the handler returns 0 (flags not consumed), clear them here.
   * @see adc_ads131m02.c adsFastDmaCompleteHandler() */
  extern int adsFastDmaCompleteHandler(void);
  if (!adsFastDmaCompleteHandler())
      GPDMA1_Channel1->CFCR = DMA_CFCR_TCF | DMA_CFCR_HTF | DMA_CFCR_DTEF |
                               DMA_CFCR_ULEF | DMA_CFCR_USEF | DMA_CFCR_SUSPF |
                               DMA_CFCR_TOF;
  return;
  /* USER CODE END GPDMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 1 */

  /* USER CODE END GPDMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles ADC1 global interrupt.
  */
void ADC1_IRQHandler(void)
{
  /* USER CODE BEGIN ADC1_IRQn 0 */

  /* USER CODE END ADC1_IRQn 0 */
  HAL_ADC_IRQHandler(&hadc1);
  /* USER CODE BEGIN ADC1_IRQn 1 */

  /* USER CODE END ADC1_IRQn 1 */
}

/**
  * @brief This function handles TIM2 global interrupt.
  */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */

  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */

  /* USER CODE END TIM2_IRQn 1 */
}

/**
  * @brief This function handles TIM3 global interrupt.
  */
void TIM3_IRQHandler(void)
{
  /* USER CODE BEGIN TIM3_IRQn 0 */

  /* USER CODE END TIM3_IRQn 0 */
  HAL_TIM_IRQHandler(&htim3);
  /* USER CODE BEGIN TIM3_IRQn 1 */

  /* USER CODE END TIM3_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt.
  */
void TIM6_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_IRQn 0 */

  /* USER CODE END TIM6_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_IRQn 1 */

  /* USER CODE END TIM6_IRQn 1 */
}

/**
  * @brief This function handles SPI1 global interrupt.
  */
void SPI1_IRQHandler(void)
{
  /* USER CODE BEGIN SPI1_IRQn 0 */

  /* USER CODE END SPI1_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi1);
  /* USER CODE BEGIN SPI1_IRQn 1 */

  /* USER CODE END SPI1_IRQn 1 */
}

/**
  * @brief This function handles SPI2 global interrupt.
  */
void SPI2_IRQHandler(void)
{
  /* USER CODE BEGIN SPI2_IRQn 0 */

  /* USER CODE END SPI2_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi2);
  /* USER CODE BEGIN SPI2_IRQn 1 */

  /* USER CODE END SPI2_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USB FS global interrupt.
  */
void USB_DRD_FS_IRQHandler(void)
{
  /* USER CODE BEGIN USB_DRD_FS_IRQn 0 */

  /* USER CODE END USB_DRD_FS_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_DRD_FS);
  /* USER CODE BEGIN USB_DRD_FS_IRQn 1 */

  /* USER CODE END USB_DRD_FS_IRQn 1 */
}

/**
  * @brief This function handles SDMMC1 global interrupt.
  */
void SDMMC1_IRQHandler(void)
{
  /* USER CODE BEGIN SDMMC1_IRQn 0 */

  /* USER CODE END SDMMC1_IRQn 0 */
  HAL_SD_IRQHandler(&hsd1);
  /* USER CODE BEGIN SDMMC1_IRQn 1 */

  /* USER CODE END SDMMC1_IRQn 1 */
}

/**
  * @brief This function handles FPU global interrupt.
  */
void FPU_IRQHandler(void)
{
  /* USER CODE BEGIN FPU_IRQn 0 */

  /* USER CODE END FPU_IRQn 0 */
  /* USER CODE BEGIN FPU_IRQn 1 */

  /* USER CODE END FPU_IRQn 1 */
}

/**
  * @brief This function handles Instruction cache global interrupt.
  */
void ICACHE_IRQHandler(void)
{
  /* USER CODE BEGIN ICACHE_IRQn 0 */

  /* USER CODE END ICACHE_IRQn 0 */
  HAL_ICACHE_IRQHandler();
  /* USER CODE BEGIN ICACHE_IRQn 1 */

  /* USER CODE END ICACHE_IRQn 1 */
}

/**
  * @brief This function handles CORDIC global interrupt.
  */
void CORDIC_IRQHandler(void)
{
  /* USER CODE BEGIN CORDIC_IRQn 0 */

  /* USER CODE END CORDIC_IRQn 0 */
  HAL_CORDIC_IRQHandler(&hcordic);
  /* USER CODE BEGIN CORDIC_IRQn 1 */

  /* USER CODE END CORDIC_IRQn 1 */
}

/**
  * @brief This function handles FMAC global interrupt.
  */
void FMAC_IRQHandler(void)
{
  /* USER CODE BEGIN FMAC_IRQn 0 */

  /* USER CODE END FMAC_IRQn 0 */
  HAL_FMAC_IRQHandler(&hfmac);
  /* USER CODE BEGIN FMAC_IRQn 1 */

  /* USER CODE END FMAC_IRQn 1 */
}

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
