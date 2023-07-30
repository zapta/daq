
#include "main.h"

#include <unistd.h>

#include "FreeRTOS.h"
#include "adc.h"
#include "cdc_serial.h"
#include "data_recorder.h"
#include "dma.h"
#include "gpio.h"
#include "host_link.h"
#include "io.h"
#include "logger.h"
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
  serial::serial2.init();

  HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
  // Set TIM12 PWM to 40%. This signal acts as
  // ADC ontinuos CS.
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 400);
  HAL_TIM_Base_Start_IT(&htim12);

  // if (!data_recorder::open_log_file("default.log")) {
  //   logger.error("Failed to open default log file.");
  // }

  host_link::setup(serial::serial1);
  if (!host_link_rx_task.start()) {
    Error_Handler();
  }
  if (!adc_task.start()) {
    Error_Handler();
  }

  Elappsed report_timer;

  for (uint32_t i = 0;; i++) {
    const bool is_logging = data_recorder::is_recording_active();
    const bool blink = is_logging ? i & 0x01 : i & 0x04;
    io::LED.set(blink);

    if (report_timer.elapsed_millis() >= 5000) {
      report_timer.reset();
      static data_recorder::RecordingInfo recording_info;
      data_recorder::get_recoding_info(&recording_info);
      if (recording_info.recording_active) {
        logger.info("Recording [%s], %lu msecs.",
                    recording_info.recording_name.c_str(),
                    recording_info.recording_time_millis);
      } else {
        logger.info("Data recorder off.");
      }
    }

    time_util::delay_millis(100);
  }
}
