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
#include "stm32f1xx_hal.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ACCEL_INT1_Pin GPIO_PIN_1
#define ACCEL_INT1_GPIO_Port GPIOA
#define ACCEL_INT1_EXTI_IRQn EXTI1_IRQn
#define ACCEL_INT0_Pin GPIO_PIN_2
#define ACCEL_INT0_GPIO_Port GPIOA
#define ACCEL_INT0_EXTI_IRQn EXTI2_IRQn
#define ACCEL_nCS_Pin GPIO_PIN_4
#define ACCEL_nCS_GPIO_Port GPIOA
#define ACCEL_SCK_Pin GPIO_PIN_5
#define ACCEL_SCK_GPIO_Port GPIOA
#define ACCEL_MISO_Pin GPIO_PIN_6
#define ACCEL_MISO_GPIO_Port GPIOA
#define ACCEL_MOSI_Pin GPIO_PIN_7
#define ACCEL_MOSI_GPIO_Port GPIOA
#define IMU_INT1_Pin GPIO_PIN_4
#define IMU_INT1_GPIO_Port GPIOC
#define IMU_INT1_EXTI_IRQn EXTI4_IRQn
#define IMU_INT3_Pin GPIO_PIN_5
#define IMU_INT3_GPIO_Port GPIOC
#define LED_B_Pin GPIO_PIN_1
#define LED_B_GPIO_Port GPIOB
#define LED_G_Pin GPIO_PIN_10
#define LED_G_GPIO_Port GPIOB
#define LED_R_Pin GPIO_PIN_11
#define LED_R_GPIO_Port GPIOB
#define MEM_nCS_Pin GPIO_PIN_12
#define MEM_nCS_GPIO_Port GPIOB
#define MEM_SCK_Pin GPIO_PIN_13
#define MEM_SCK_GPIO_Port GPIOB
#define MEM_MISO_Pin GPIO_PIN_14
#define MEM_MISO_GPIO_Port GPIOB
#define MEM_MOSI_Pin GPIO_PIN_15
#define MEM_MOSI_GPIO_Port GPIOB
#define MEM_nWP_Pin GPIO_PIN_7
#define MEM_nWP_GPIO_Port GPIOC
#define MEM_nHOLD_Pin GPIO_PIN_8
#define MEM_nHOLD_GPIO_Port GPIOC
#define LoRa_TX_Pin GPIO_PIN_10
#define LoRa_TX_GPIO_Port GPIOC
#define LoRa_RX_Pin GPIO_PIN_11
#define LoRa_RX_GPIO_Port GPIOC
#define IMU_ALT_SCK_Pin GPIO_PIN_3
#define IMU_ALT_SCK_GPIO_Port GPIOB
#define IMU_ALT_MISO_Pin GPIO_PIN_4
#define IMU_ALT_MISO_GPIO_Port GPIOB
#define IMU_ALT_MOSI_Pin GPIO_PIN_5
#define IMU_ALT_MOSI_GPIO_Port GPIOB
#define IMU_GYRO_nCS_Pin GPIO_PIN_6
#define IMU_GYRO_nCS_GPIO_Port GPIOB
#define IMU_ACCEL_nCS_Pin GPIO_PIN_7
#define IMU_ACCEL_nCS_GPIO_Port GPIOB
#define ALT_nCS_Pin GPIO_PIN_8
#define ALT_nCS_GPIO_Port GPIOB
#define TC_nCS_Pin GPIO_PIN_9
#define TC_nCS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
