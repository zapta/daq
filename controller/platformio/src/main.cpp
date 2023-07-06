
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

// static SerialPacketsData data;

StaticTask<2000> host_link_rx_task(host_link::rx_task_body, "Host RX", 8);
StaticTask<2000> adc_task(adc::adc_task_body, "ADC", 7);

// Called from from the main FreeRTOS task.
void app_main() {
  serial::serial1.init();

  HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
  // Set TIM12 PWM to 40%. This signal acts as
  // ADC ontinuos CS.
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 400);
  HAL_TIM_Base_Start_IT(&htim12);

  // if (!sd::open_log_file("default.log")) {
  //   logger.error("Failed to open default log file.");
  // }

  host_link::setup(serial::serial1);
  if (!host_link_rx_task.start()) {
    Error_Handler();
  }
  if (!adc_task.start()) {
    Error_Handler();
  }

  for (int i = 1;; i++) {
    // adc::dump_state();
    io::LED.toggle();
    // Send a periodic test command.
    // data.clear();
    // data.write_uint32(0x12345678);
    // const PacketStatus status = host_link::client.sendCommand(0x20, data);
    // logger.info("%04d: Recieced command respond, status = %d, size=%hu", i, status,
    //             data.size());
    if (!sd::is_log_file_idle() && !sd::is_log_file_open_ok()) {
      logger.error("SD log file not opened.");
    }
    time_util::delay_millis(500);
  }
}
