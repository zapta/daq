/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include "stm32h7xx_hal.h"

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
#define LED_Pin GPIO_PIN_3
#define LED_GPIO_Port GPIOE
#define USER_SWITCH_Pin GPIO_PIN_13
#define USER_SWITCH_GPIO_Port GPIOC
#define ADC_SCK_Pin GPIO_PIN_5
#define ADC_SCK_GPIO_Port GPIOA
#define ADC_MISO_Pin GPIO_PIN_6
#define ADC_MISO_GPIO_Port GPIOA
#define ADC_MOSI_Pin GPIO_PIN_7
#define ADC_MOSI_GPIO_Port GPIOA
#define ADC_CS_PWM_Pin GPIO_PIN_14
#define ADC_CS_PWM_GPIO_Port GPIOB
#define IFC_IN1_Pin GPIO_PIN_6
#define IFC_IN1_GPIO_Port GPIOC
#define IFC_OUT1_Pin GPIO_PIN_7
#define IFC_OUT1_GPIO_Port GPIOC
#define TEST1_Pin GPIO_PIN_1
#define TEST1_GPIO_Port GPIOD
#define SD_SWITCH_Pin GPIO_PIN_4
#define SD_SWITCH_GPIO_Port GPIOD
#define DISPLAY_SCL_Pin GPIO_PIN_8
#define DISPLAY_SCL_GPIO_Port GPIOB
#define DISPLAY_SDA_Pin GPIO_PIN_9
#define DISPLAY_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
