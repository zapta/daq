
#include "main.h"

#include "cmsis_os.h"
#include "gpio.h"
#include "usart.h"
#include "usb_device.h"

void SystemClock_Config(void);
void MX_FREERTOS_Init(void);

int main() {
  HAL_Init();

  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART1_UART_Init();

  // Call init function for freertos objects (in freertos.c)
  osKernelInitialize();
  MX_FREERTOS_Init();
  osKernelStart();
  /* We should never get here as control is now taken by the scheduler */
  while (1) {
  }
}