#include "i2c_handler.h"

#include <FreeRtos.h>
#include <i2c.h>

#include "common.h"
#include "error_handler.h"
#include "static_queue.h"
#include "time_util.h"

// TODO: More graceful handling of errors (?)

namespace i2c_handler {

static constexpr uint8_t kAds1115DeviceAddress = 0x48 << 1;

// Sampling time 1/128 sec. 4.096V full scale. Single mode.
static constexpr uint16_t kAds1115BaseConfig = 0b0000001100100000;
static constexpr uint16_t kAds1115ConfigStartCh0 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0100 << 12;
static constexpr uint16_t kAds1115ConfigStartCh1 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0101 << 12;

// Buffer for DMA transactions.
static uint8_t data_buffer[5];

// Current ADC channel. Alternates between 0 and 1.
static int ch = 0;

// Module states.
enum State {
  // Initial state, before setup.
  STATE_UNDEFINED,
  // Ready for next sampling cycle.
  STATE_IDLE,
  // Started I2C operation 1 (select register 0 to read)
  STATE_STEP1,
  // Started I2C operation 2 (read ADC value from selected register.)
  STATE_STEP2,
  // Started I2c operation 3 (start convertion of next channel)
  STATE_STEP3
};

static State state = STATE_UNDEFINED;

// The IRQ sequence sends these events to the processing task, each
// time a new convertion value is read from the ADC.
struct IrqEvent {
  uint32_t timestamp_millis;
  int ch;
  int16_t adc_value;
};

static StaticQueue<IrqEvent, 5> irq_event_queue;

// Hanlers for STEP1.
namespace step1 {
// Start state 1. Called from timer callback. Should be non blocking.
static inline void start_from_timer() {
  if (state != STATE_IDLE) {
    error_handler::Panic(216);
  }
  static_assert(sizeof(data_buffer[0]) == 1);
  static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 1);
  data_buffer[0] = 0;
  state = STATE_STEP1;
  HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, kAds1115DeviceAddress, data_buffer, 1);
  if (status != HAL_OK) {
    error_handler::Panic(217);
  }
}

// Nothing to do here.
static inline void on_completion_from_isr() {}
}  // namespace step1

// Hanlers for STEP2.
namespace step2 {
// Start step 2. Called from isr.
static void start_from_isr() {
  if (state != STATE_STEP1) {
    error_handler::Panic(217);
  }
  // Start reading the conversion value from the selected register 0.
  static_assert(sizeof(data_buffer[0]) == 1);
  static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 2);
  data_buffer[0] = 0;
  data_buffer[1] = 0;
  state = STATE_STEP2;
  const HAL_StatusTypeDef status =
      HAL_I2C_Master_Receive_DMA(&hi2c1, kAds1115DeviceAddress, data_buffer, 2);
  if (status != HAL_OK) {
    error_handler::Panic(212);
  }
}

static void on_completion_from_isr(BaseType_t* task_woken) {
  if (state != STATE_STEP2) {
    error_handler::Panic(218);
  }
  // Here when completed to read the conversion value from reg 0.
  // Use the value conversion value.
  const uint16_t reg_value = ((uint16_t)data_buffer[0] << 8) | data_buffer[1];
  const IrqEvent event = {.timestamp_millis = time_util::millis_from_isr(),
                          .ch = ch,
                          .adc_value = (int16_t)reg_value};
  if (!irq_event_queue.add_from_isr(event, task_woken)) {
    // Comment this out for debugging with breakpoints
    error_handler::Panic(214);
  }
}
}  // namespace step2

// Hanlers for STEP3.
namespace step3 {
// Start conversion of current channel.
static inline void start_from_isr() {
  if (state != STATE_STEP2) {
    error_handler::Panic(219);
  }
  const uint16_t config_value =
      ch == 0 ? kAds1115ConfigStartCh0 : kAds1115ConfigStartCh1;
  static_assert(sizeof(data_buffer[0]) == 1);
  static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 3);
  data_buffer[0] = 0x01;  // config reg address
  data_buffer[1] = (uint8_t)(config_value >> 8);
  data_buffer[2] = (uint8_t)config_value;
  state = STATE_STEP3;
  const HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, kAds1115DeviceAddress, data_buffer, 3);
  if (status != HAL_OK) {
    error_handler::Panic(215);
  }
}

static inline void on_completion_from_isr() {
  if (state != STATE_STEP3) {
    error_handler::Panic(221);
  }
  // I2C transaction sequence completed.
  state = STATE_IDLE;
}
}  // namespace step3

// A shared handler for RX and TX completions.
void i2c_MasterCallbackIsr(I2C_HandleTypeDef* hi2c) {
  // TODO: Implement.
  switch (state) {
    case STATE_STEP1:
      step1::on_completion_from_isr();
      step2::start_from_isr();
      break;

    case STATE_STEP2: {
      BaseType_t task_woken = pdFALSE;
      step2::on_completion_from_isr(&task_woken);
      ch = (ch + 1) & 0x01;
      step3::start_from_isr();
      // In case the queue push above requires a task switch.
      portYIELD_FROM_ISR(task_woken)

    } break;

    case STATE_STEP3:
      // Cycle completed OK.
      state = STATE_IDLE;
      break;

    default:
      error_handler::Panic(211);
  }
}

void i2c_ErrorCallbackIsr(I2C_HandleTypeDef* hi2c) {
  // TODO: Implement.
  error_handler::Panic(117);
}

void i2c_AbortCallbackIsr(I2C_HandleTypeDef* hi2c) {
  // TODO: Implement.
  error_handler::Panic(118);
}

void setup() {
  if (state != STATE_UNDEFINED) {
    error_handler::Panic(119);
  }

  // Register interrupt handler. These handler are marked in
  // cube ide for registration rather than overriding a weak
  // global handler.
  //
  // NOTE: We use a shared handler for TX and RX completions.
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1,
                                         HAL_I2C_MASTER_TX_COMPLETE_CB_ID,
                                         i2c_MasterCallbackIsr)) {
    error_handler::Panic(111);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1,
                                         HAL_I2C_MASTER_RX_COMPLETE_CB_ID,
                                         i2c_MasterCallbackIsr)) {
    error_handler::Panic(112);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1, HAL_I2C_ERROR_CB_ID,
                                         i2c_ErrorCallbackIsr)) {
    error_handler::Panic(113);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1, HAL_I2C_ABORT_CB_ID,
                                         i2c_AbortCallbackIsr)) {
    error_handler::Panic(114);
  }

  state = STATE_IDLE;
}

void i2c_task_body(void* argument) {
  // Processing loop.
  for (;;) {
    // time_util::delay_millis(100);

    // const uint32_t n = irq_event_queue.size();

    // i2c_timer_cb();

    // logger.info("I2C: Queue has %lu items", n);
    // for (uint32_t i = 0; i < n; i++) {
    IrqEvent event;
    if (!irq_event_queue.consume_from_task(&event, portMAX_DELAY)) {
      error_handler::Panic(122);
    }

    logger.info("I2C[%d]: %lu, %hd", event.ch, event.timestamp_millis,
                event.adc_value);
  }
}

// This function is called from the timer daemon and thus should be non blocking.
void i2c_timer_cb(TimerHandle_t xTimer) { step1::start_from_timer(); }

}  // namespace i2c_handler