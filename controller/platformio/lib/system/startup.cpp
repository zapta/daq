
#include <unistd.h>

#include "FreeRTOS.h"
#include "cdc_serial.h"
#include "dma.h"
#include "gpio.h"
#include "io.h"
#include "logger.h"
#include "main.h"
#include "serial.h"
#include "spi.h"
#include "tim.h"
#include "static_task.h"
#include "usart.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

void app_main();

// Implemented in cube ide generated main.c.
extern "C" void SystemClock_Config(void);

// For OpenOCD thread awareness. Per
// https://community.platformio.org/t/freertos-with-stm32cube-framework-on-nucleof767zi/9601
extern "C" {
extern const int uxTopUsedPriority;
__attribute__((section(".rodata"))) const int uxTopUsedPriority =
    configMAX_PRIORITIES - 1;
}

void main_task_body(void* argument);
StaticTask<2000> main_task(main_task_body, "Main", 3);

StaticTask<2000> cdc_logger_task(cdc_serial::logger_task_body, "Logger", 10);

void main_task_body(void* argument) {
  // NOTE: We delay to give the CDC chance to connect so we don't
  // loose the initial printouts.   
  MX_USB_DEVICE_Init();
  HAL_Delay(1000);  // Let it connect.
  if (!cdc_logger_task.start()) {
    Error_Handler();
  }
  logger.set_level(LOG_INFO);
  logger.info("Serial USB started");

  // Should not return.
  app_main();
  Error_Handler();
}

// Based on lib/autogen_core/main.c.ignore
int main(void) {
  // NOTE: Systick disabled until the FreeRTOS scheduler starts
  // so avoid calling here HAL_Delay or other functionality that
  // relyes on systick. This was observed also with Cube IDE when
  // FreeRTOS is eanbled.

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();

  if (!main_task.start()) {
    Error_Handler();
  }

  // Should not return.
  vTaskStartScheduler();
  Error_Handler();
}
