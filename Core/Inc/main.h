/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
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
#define CHG_STAT1_Pin GPIO_PIN_5
#define CHG_STAT1_GPIO_Port GPIOB
#define CHG_STAT2_Pin GPIO_PIN_6
#define CHG_STAT2_GPIO_Port GPIOB
#define CHG_PG_Pin GPIO_PIN_7
#define CHG_PG_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
