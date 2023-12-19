
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
#include "logger.h"
#include "main.h"
#include "printer_link_card.h"
#include "pw_card.h"
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
static StaticTask host_link_task(host_link::host_link_task_body, "Host", 6);
static StaticTask printer_link_task(printer_link_card::printer_link_task_body,
                                    "Printer Link", 3);
static StaticTask adc_card_task(adc_card::adc_card_task_body, "ADC", 5);
static StaticTask pw_card_task(pw_card::i2c1_pw1_device_task_body, "PW1", 7);
static StaticTask data_queue_task(data_queue::data_queue_task_body, "DQUE", 4);

// I2c schedule
static I2cSchedule i2c1_schedule = {
    // 10ms per cycle (100hz cycles)
    .ms_per_slot = 2,
    .slots_per_cycle = 5,
    .slots = {
        // For power device, two slots form a data points.
        // With a divider of 2, the data point interval is 40ms (25 Hz)
        [0] = {.device = &pw_card::i2c1_pw1_device, .rate_divider = 2},
    }};

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
  if (!host_link_task.start()) {
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

  // Start the I2c schedulers. Must be done after the i2c devices
  // were initialized (e.g. via their tasks).
  if (!i2c_scheduler::i2c1_scheduler.start(&i2c1_schedule)) {
    error_handler::Panic(131);
  }

  // Start the main loop. It's used to provide visual feedback to the user.
  Elappsed report_timer;

  for (uint32_t i = 0;; i++) {
    const bool is_logging = data_recorder::is_recording_active();
    // We invert the bits of i to start blinking with on state.
    const bool led_state = is_logging ? ~i & 0x01 : ~i & 0x08;
    gpio_pins::LED.set(led_state);

    if (report_timer.elapsed_millis() >= 5000) {
      report_timer.reset();
      static data_recorder::RecordingInfo recording_info;
      data_recorder::get_recoding_info(&recording_info);
      if (recording_info.recording_active) {
        logger.info(
            "Recording [%s], %lu ms.", recording_info.recording_name.c_str(),
            time_util::millis() - recording_info.recording_start_time_millis);
      }
      logger.info("Session id: [%08lx]", session::id());
      data_queue::dump_state();
      adc_card::verify_static_registers_values();
    }

    time_util::delay_millis(100);
  }
}
