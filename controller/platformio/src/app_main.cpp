
#include <FreeRTOS.h>
#include <unistd.h>

#include "adc_card.h"
#include "cdc_serial.h"
#include "data_queue.h"
#include "data_recorder.h"
#include "dma.h"
#include "gpio.h"
#include "gpio_pins.h"
#include "host_link.h"
#include "pw_card.h"
#include "logger.h"
#include "main.h"
#include "printer_link_card.h"
#include "serial.h"
#include "session.h"
#include "spi.h"
#include "static_task.h"
#include "tim.h"
#include "usart.h"
#include "usbd_cdc_if.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

// Tasks with static stack allocations.
StaticTask<2000> host_link_rx_task(host_link::host_link_task_body, "Host RX", 6);
StaticTask<2000> printer_link_task(printer_link_card::printer_link_task_body, "Printer Link",
                                      3);
StaticTask<2000> adc_card_task(adc_card::adc_card_task_body, "ADC", 5);
StaticTask<2000> pw_card_task(pw_card::pw_card_task_body, "PW", 7);
StaticTask<2000> data_queue_task(data_queue::data_queue_task_body, "DQUE", 4);

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

  // Init data queue.
  data_queue::setup();

  // Init host link.
  host_link::setup(serial::serial1);

  // Init printer link.
  printer_link_card::setup(&serial::serial2);

  // Start tasks.
  if (!data_queue_task.start()) {
    error_handler::Panic(69);
  }
  if (!host_link_rx_task.start()) {
    error_handler::Panic(86);
  }
  if (!printer_link_task.start()) {
    error_handler::Panic(87);
  }
  if (!adc_card_task.start()) {
    error_handler::Panic(88);
  }
  if (!pw_card_task.start()) {
    error_handler::Panic(88);
  }

  // Start the main loop. It's used to provide visual feedback to the user.
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
      data_queue::dump_state();
      adc_card::verify_static_registers_values();
    }

    time_util::delay_millis(100);
  }
}
