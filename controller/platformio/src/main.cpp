
#include "main.h"

#include <unistd.h>

#include "FreeRTOS.h"
#include "cdc_serial.h"
#include "gpio.h"
#include "host_link.h"
#include "io.h"
#include "logger.h"
#include "serial.h"
#include "tasks.h"
#include "usart.h"
#include "usbd_cdc_if.h"

extern "C" void SystemClock_Config(void);

// For OpenOCD thread awareness. Per
// https://community.platformio.org/t/freertos-with-stm32cube-framework-on-nucleof767zi/9601
extern "C" {
extern const int uxTopUsedPriority;
__attribute__((section(".rodata"))) const int uxTopUsedPriority =
    configMAX_PRIORITIES - 1;
}

static SerialPacketsData message_data;

void main_task_body(void* argument) {
  MX_GPIO_Init();

  // Initialize the logger. This enables the logger.
  cdc_serial::setup();
  if (!tasks::cdc_logger_task.start()) {
    Error_Handler();
  }
  logger.set_level(LOG_INFO);
  logger.info("Serial USB started");

  // Init host link via serial 1.
  MX_USART1_UART_Init();
  serial::serial1.init();
  host_link::setup(serial::serial1);
  if (!tasks::host_link_rx_task.start()) {
    Error_Handler();
  }

  int i = 0;
  for (;;) {
    // util::dump_heap_stats();

    io::LED.toggle();

    message_data.clear();
    message_data.write_uint32(0x12345678);
    const bool ok = host_link::client.sendMessage(0x20, message_data);
    logger.info("%04d: Sent message to port 0x20, %hu data bytes, ok=%d", i++,
                message_data.size(), ok);
    // logger.info("Free stacks: %hu, %hu, %hu",
    //             tasks::main_task.unused_stack_bytes(),
    //             tasks::cdc_logger_task.unused_stack_bytes(),
    //             tasks::host_link_rx_task.unused_stack_bytes());

    vTaskDelay(500);
  }
}

// Based on lib/autogen_core/main.c.ignore
int main(void) {
  // We perform the minimum initialization required to start
  // FreeRTOS and the main task, and then do the rest
  // in the main task..
  HAL_Init();
  SystemClock_Config();

  // TaskHandle_t xHandle = NULL;
  // xTaskCreate(main_task, "Main", 1000 / sizeof(StackType_t), nullptr, 10,
  //             &xHandle);
  if (!tasks::main_task.start()) {
    Error_Handler();
  }

  // Normally, this never returns.
  vTaskStartScheduler();
  Error_Handler();
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {
  }
}
