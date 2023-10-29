
#include "main.h"
#include <unistd.h>
#include "FreeRTOS.h"
#include "adc.h"
#include "cdc_serial.h"
#include "data_recorder.h"
#include "dma.h"
#include "gpio.h"
#include "host_link.h"
#include "gpio_pins.h"
#include "logger.h"
#include "printer_link.h"
#include "serial.h"
#include "session.h"
#include "spi.h"
#include "i2c_handler.h"
#include "static_task.h"
#include "tim.h"
#include "usart.h"
#include "usbd_cdc_if.h"

// Tasks with static stack allocations.
StaticTask<2000> host_link_rx_task(host_link::rx_task_body, "Host RX", 6);
StaticTask<2000> printer_link_rx_task(printer_link::rx_task_body, "Printer RX", 4);
StaticTask<2000> adc_task(adc::adc_task_body, "ADC", 5);
StaticTask<2000> i2c_task(i2c::i2c_task_body, "I2C", 7);

// Called from from the main FreeRTOS task.
void app_main() {
  session::setup();

  serial::serial1.init();
  serial::serial2.init();

  HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
  // Set TIM12 PWM 200 ticks high, 300 ticks down. Acts
  // as CS for the ADC SPI.
  __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 200);
  HAL_TIM_Base_Start_IT(&htim12);

  // Init host link.
  host_link::setup(serial::serial1);

  // Init printer link.
  printer_link::setup(serial::serial2);

  // Start tasks.
  if (!host_link_rx_task.start()) {
    error_handler::Panic(86);
  }
  if (!printer_link_rx_task.start()) {
    error_handler::Panic(87);
  }
  if (!adc_task.start()) {
    error_handler::Panic(88);
  }
   if (!i2c_task.start()) {
    error_handler::Panic(88);
  }

  Elappsed report_timer;

  for (uint32_t i = 0;; i++) {
    const bool is_logging = data_recorder::is_recording_active();
    const bool blink = is_logging ? i & 0x01 : i & 0x04;
    gpio_pins::LED.set(blink);

    if (report_timer.elapsed_millis() >= 5000) {
      report_timer.reset();
      static data_recorder::RecordingInfo recording_info;
      data_recorder::get_recoding_info(&recording_info);
      if (recording_info.recording_active) {
        logger.info("Recording [%s], %lu msecs.",
                    recording_info.recording_name.c_str(),
                    recording_info.recording_time_millis);
      }
      logger.info("Session id: [%08lx]", session::id());
      adc::verify_registers_vals();
    }

    time_util::delay_millis(100);
  }
}
