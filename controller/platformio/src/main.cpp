
#include "main.h"

#include <unistd.h>

#include "FreeRTOS.h"
#include "cdc_serial.h"
#include "gpio.h"
#include "io.h"
#include "logger.h"
#include "serial.h"
#include "task.h"
#include "usart.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "util.h"


extern "C"
void SystemClock_Config(void);




void rx_task(void *argument) {
  for (;;) {
    // const uint32_t t1 = util::task_stack_unused_bytes();
    static uint8_t rx_buffer[30];
    const uint16_t bytes_read =
        serial::serial1.read(rx_buffer, sizeof(rx_buffer) - 1);
    rx_buffer[bytes_read] = 0;  // string terminator
    logger.info("RX %hu: [%s]", util::task_stack_unused_bytes(),
                (char *)rx_buffer);
  }
}

void main_task(void *argument) {
  // Do not use printf() or logger before calling cdc_serial::setup() here.
  cdc_serial::setup();
  logger.info("Serial USB started");
  util::dump_heap_stats();

  MX_GPIO_Init();
  MX_USART1_UART_Init();
  serial::serial1.init();

  TaskHandle_t xHandle = NULL;
  xTaskCreate(rx_task, "RX1", 1000 / sizeof(StackType_t), nullptr, 10,
              &xHandle);

  int i = 0;
  for (;;) {
    // serial::serial1.write_str("12345\n");
    io::LED.toggle();
    logger.info("%04d: %lu bytes", i++, util::task_stack_unused_bytes());
    // util::dump_heap_stats();
    vTaskDelay(1000);
  }
}

// For OpenOCD thread awareness. Per
// https://community.platformio.org/t/freertos-with-stm32cube-framework-on-nucleof767zi/9601
extern "C" {
extern const int uxTopUsedPriority;
__attribute__((section(".rodata"))) const int uxTopUsedPriority =
    configMAX_PRIORITIES - 1;
}

// Based on lib/autogen_core/main.c.ignore
int main(void) {
  // We perform the minimum initialization required to start
  // FreeRTOS and the main task, and then do the rest
  // in the main task..
  HAL_Init();
  SystemClock_Config();
  TaskHandle_t xHandle = NULL;
  xTaskCreate(main_task, "Main", 1000 / sizeof(StackType_t), nullptr, 10,
              &xHandle);
  // Normally, this never returns.
  vTaskStartScheduler();
  Error_Handler();
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {
  }
}
