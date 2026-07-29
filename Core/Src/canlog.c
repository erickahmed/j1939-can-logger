/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : canlog.c
  * @brief          : SAE J1939 CAN Real Time listening and decoding
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Erick Ahmed.
  *
  * SPDX-License-Identifier: GPL-3.0-or-later
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "canlog.h"
#include "main.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cansend.h"
#include "cmsis_os.h"
#include <string.h>
/* USER CODE END Includes */

extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

extern osThreadId_t xCAN1LedTask;
extern osThreadId_t xCAN2LedTask;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
/**
  * @brief  Initializes the CAN logger modules, OS threads, queues, and hardware.
  * @param  hcan1 Pointer to the CAN1 handle
  * @param  hcan2 Pointer to the CAN2 handle
  * @retval None
  */
void CAN_Logger_Init(CAN_HandleTypeDef *hcan1, CAN_HandleTypeDef *hcan2)
{
    CAN_FilterTypeDef filter = {0};
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x0000;
    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterIdLow = 0x0000;
    filter.FilterMaskIdLow = 0x0000;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    filter.FilterBank = 0;
    if (HAL_CAN_ConfigFilter(hcan1, &filter) != HAL_OK) Error_Handler();

    filter.FilterBank = 14;
    if (HAL_CAN_ConfigFilter(hcan2, &filter) != HAL_OK) Error_Handler();
}
/* END CAN_Logger_Init */

/* BEGIN HAL_CAN_RxFifo0MsgPendingCallback */
/**
  * @brief  ISR for CAN message pending in FIFO0
  * @param  hcan: CAN handle hcan (hcan1 or hcan2)
  * @retval None
  */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CanMessage_t message;
    CAN_RxHeaderTypeDef rxHeader;
    uint8_t data[8];

    osMessageQueueId_t queue;
    osThreadId_t led_task;

    // TODO: manage the case of FIFO overflow
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rxHeader, data) != HAL_OK) return;

    if (rxHeader.IDE == CAN_ID_EXT) message.id = rxHeader.ExtId;
    else message.id = rxHeader.StdId;
    message.dlc = rxHeader.DLC;
    message.isExtended = (rxHeader.IDE == CAN_ID_EXT);
    message.source = (hcan->Instance == CAN1) ? 1 : 2;
    memcpy(message.payload, data, rxHeader.DLC);

    if (hcan->Instance == CAN1)
    {
        queue    = xCAN1RxQueue;
        led_task = xCAN1LedTask;
    }
    else
    {
        queue    = xCAN2RxQueue;
        led_task = xCAN2LedTask;
    }

    if (osMessageQueuePut(queue, &message, 0U, 0U) == osOK) osThreadFlagsSet(led_task, 0x01);
}
/* END HAL_CAN_RxFifo0MsgPendingCallback */

/* BEGIN vCANListener */
/**
  * @brief  Log incoming CAN bus traffic in FIFO buffer
  * @param  argument: Not used
  * @retval None
  */
void vCANListener(void *argument)
{
  CAN_HandleTypeDef *hcan = (CAN_HandleTypeDef*)argument;
  osMessageQueueId_t queue = (hcan->Instance == CAN1) ? xCAN1RxQueue : xCAN2RxQueue;
  CanMessage_t message;

  if (HAL_CAN_Start(hcan) != HAL_OK) Error_Handler();
  HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);

  uint32_t irqn = (hcan->Instance == CAN1) ? CAN1_RX0_IRQn : CAN2_RX0_IRQn;
  HAL_NVIC_SetPriority(irqn, 5, 0);
  HAL_NVIC_EnableIRQ(irqn);

  for (;;)
  {
    if (osMessageQueueGet(queue, &message, NULL, 1000) == osOK)
    {
          // J1939 decoding

      osMessageQueuePut(xUARTQueue, &message, 0U, 0U);
    }
  }
}
/* END vCANListener */

/* BEGIN vLEDHeartbeat */
/**
  * @brief  Turn on LED when CAN traffic is detected
  * @param  argument: LED GPIO (port and pin)
  * @retval None
  */
void vLEDHeartbeat(void *argument)
{
  LED_Config *led = (LED_Config*)argument;

  for (;;)
  {
    uint32_t notification = osThreadFlagsWait(0x01, osFlagsWaitAny, osWaitForever);

    if (notification & 0x01)
    {
      HAL_GPIO_WritePin(led->port, led->pin, GPIO_PIN_SET);
      osDelay(LED_BLINK_MS);
      HAL_GPIO_WritePin(led->port, led->pin, GPIO_PIN_RESET);
    }
  }
}
/* END vLEDHeartbeat */
/* USER CODE END FunctionPrototypes */
