// This file contains the startup code which initializes
// the cube_ide defined peripherals and starts the main thread
// of FREERTOS which calls app_main(). app_main() can be the 
// application main code or a unit test when testing with Unity.

#include <unistd.h>

#include "FreeRTOS.h"
#include "cdc_serial.h"
#include "dma.h"
#include "fatfs.h"
#include "gpio.h"
#include "logger.h"
#include "main.h"
#include "rng.h"
#include "i2c.h"
#include "sdmmc.h"
#include "serial.h"
#include "spi.h"
#include "static_task.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

// #pragma GCC push_options
// #pragma GCC optimize("O0")

// There are several implementations of app_main(). One for the 
// app (for release or debug modes) and one for each unit test.
void app_main();

// Implemented in cube ide generated main.c.
extern "C" void SystemClock_Config(void);
extern "C" void PeriphCommonClock_Config(void);

// For OpenOCD thread awareness. Per
// https://community.platformio.org/t/freertos-with-stm32cube-framework-on-nucleof767zi/9601
extern "C" {
extern const int uxTopUsedPriority;
__attribute__((section(".rodata"))) const int uxTopUsedPriority =
    configMAX_PRIORITIES - 1;
}

static void main_task_body_impl(void* argument);
static TaskBodyFunction main_task_body(main_task_body_impl, nullptr);
static StaticTask main_task(main_task_body, "Main", 2);

static StaticTask cdc_logger_task(cdc_serial::logger_task_body, "Logger", 3);

static void main_task_body_impl(void* argument) {
  // NOTE: We delay to give the CDC chance to connect so we don't
  // loose the initial printouts.
  MX_USB_DEVICE_Init();
  HAL_Delay(1000);  // Let it connect.
  if (!cdc_logger_task.start()) {
    error_handler::Panic(91);
  }
  logger.set_level(LOG_INFO);
  logger.info("Serial USB started");
  // Make sure the symbol uxTopUsedPriority is not optimized
  // out by the linker. The OpenOCD debugger needs it.
  logger.info("uxTopUsedPriority address = %p", &uxTopUsedPriority);

  // Should not return.
  app_main();
  error_handler::Panic(92);
}

// Based on lib/autogen_core/main.c.ignore
int main(void) {
  // NOTE: Systick disabled until the FreeRTOS scheduler starts
  // so avoid calling here HAL_Delay or other functionality that
  // relyes on systick. This was observed also with Cube IDE when
  // FreeRTOS is eanbled.

  HAL_Init();
  SystemClock_Config();
  PeriphCommonClock_Config();

  // This should match the initialization in
  // lib/cube_ide/Core/src/main.c which we do not use.
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_TIM12_Init();
  MX_SDMMC1_SD_Init();
  MX_FATFS_Init();
  MX_USART2_UART_Init();
  MX_RNG_Init();
  MX_I2C1_Init();

  if (!main_task.start()) {
    error_handler::Panic(93);
  }

  // Should not return.
  vTaskStartScheduler();
  error_handler::Panic(94);
}
