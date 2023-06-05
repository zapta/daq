
#include "main.h"

#include <unistd.h>

#include "FreeRTOS.h"
#include "cdc_serial.h"
#include "gpio.h"
#include "io.h"
#include "logger.h"
#include "serial.h"
#include "serial_packets_client.h"
#include "task.h"
#include "usart.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "util.h"
#include "rtos_util.h"

extern "C" void SystemClock_Config(void);

static SerialPacketsClient serial_packets_client;

static SerialPacketsData message_data;

// For OpenOCD thread awareness. Per
// https://community.platformio.org/t/freertos-with-stm32cube-framework-on-nucleof767zi/9601
extern "C" {
extern const int uxTopUsedPriority;
__attribute__((section(".rodata"))) const int uxTopUsedPriority =
    configMAX_PRIORITIES - 1;
}


void command_handler(uint8_t endpoint, const SerialPacketsData& command_data,
                     uint8_t& response_status,
                     SerialPacketsData& response_data) {
  logger.info("Recieved a command at endpoint %02hhx", endpoint);
}

// A callback type for incoming messages.
void message_handler(uint8_t endpoint, const SerialPacketsData& message_data) {
  logger.info("Recieved a message at endpoint %02hhx", endpoint);
}

void main_task_body(void* argument);
static StaticTask<1000> main_task(main_task_body, "Main", 10);

void rx_task_body(void* argument);
static StaticTask<1000> rx_task(rx_task_body, "RX", 10);


void rx_task_body(void* argument) {
  // for (;;) {
  // This method doesn't return.
  serial_packets_client.rx_task_body();
  Error_Handler();
  // vTaskDelay(100);
  // const uint32_t t1 = util::task_stack_unused_bytes();
  // static uint8_t rx_buffer[30];
  // const uint16_t bytes_read =
  //     serial::serial1.read(rx_buffer, sizeof(rx_buffer) - 1);
  // rx_buffer[bytes_read] = 0;  // string terminator
  // logger.info("RX %hu: [%s]", util::task_stack_unused_bytes(),
  //             (char *)rx_buffer);
  // }
}


void main_task_body(void* argument) {
  // Do not use printf() or logger before calling cdc_serial::setup() here.
  cdc_serial::setup();
  logger.set_level(LOG_INFO);
  logger.info("Serial USB started");
  util::dump_heap_stats();

  MX_GPIO_Init();
  MX_USART1_UART_Init();
  serial::serial1.init();
  serial_packets_client.begin(serial::serial1, command_handler,
                              message_handler);
  // TaskHandle_t xHandle = NULL;
  // xTaskCreate(rx_task, "RX1", 1000 / sizeof(StackType_t), nullptr, 10,
  //             &xHandle);

  
  if (!rx_task.start()) {
    Error_Handler();
  }

  int i = 0;
  for (;;) {
    // util::dump_heap_stats();

    io::LED.toggle();

    message_data.clear();
    message_data.write_uint32(0x12345678);
    const bool ok = serial_packets_client.sendMessage(0x20, message_data);
    logger.info("%04d: Sent message to port 0x20, %hu data bytes, ok=%d", i++,
                message_data.size(), ok);

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
  if (!main_task.start()) {
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
