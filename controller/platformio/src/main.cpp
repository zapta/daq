
#include "main.h"

#include <unistd.h>

#include "FreeRTOS.h"
#include "adc.h"
#include "cdc_serial.h"
#include "dma.h"
#include "gpio.h"
#include "host_link.h"
#include "io.h"
#include "logger.h"
#include "sd.h"
#include "serial.h"
#include "spi.h"
#include "static_task.h"
#include "tim.h"
#include "usart.h"
#include "usbd_cdc_if.h"

static SerialPacketsData data;

StaticTask<2000> host_link_rx_task(host_link::rx_task_body, "Host RX", 10);

// Called from from the main FreeRTOS task.
void app_main() {
  serial::serial1.init();

  // HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  // __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 100);

  HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 100);
  // HAL_TIM_IRQHandler(&htim12);
  HAL_TIM_Base_Start_IT(&htim12);

  host_link::setup(serial::serial1);
  if (!host_link_rx_task.start()) {
    Error_Handler();
  }

  adc::test_setup();

  // sd::test_setup();

  for (int i = 1;; i++) {
    adc::test_loop();

    // sd::test_loop();

    io::LED.toggle();
    data.clear();
    data.write_uint32(0x12345678);

    const PacketStatus status = host_link::client.sendCommand(0x20, data);
    logger.info("%04d: Recieced respond, status = %d, size=%hu", i, status,
                data.size());
    // logger.info("Switch = %d", io::SWITCH.read());

    time_util::delay_millis(500);
  }
}
