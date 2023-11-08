#include "pw_card.h"

#include <FreeRtos.h>
#include <i2c.h>

#include "common.h"
#include "data_queue.h"
#include "error_handler.h"
#include "session.h"
#include "static_queue.h"
#include "static_timer.h"
#include "time_util.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

namespace pw_card {

static void pw_card_timer_cb(TimerHandle_t xTimer);

// Each data point contains a pair of readings for voltage
// and current respectivly.
static constexpr uint16_t kDataPointsPerPacket = 8;
static constexpr uint16_t kMsTimerTick = 25;
static constexpr uint16_t kMsPerDataPoint = 2 * kMsTimerTick;

// Timer with static allocation. 25ms interval, for 20 data points per
// seconds (each data points includes two ADC readings)
StaticTimer pw_card_timer(pw_card::pw_card_timer_cb, "PW", kMsTimerTick);

static constexpr uint8_t kAds1115DeviceAddress = 0x48 << 1;

// Sampling time 1/128 sec. 4.096V full scale. Single mode.
static constexpr uint16_t kAds1115BaseConfig = 0b0000001110000000;
static constexpr uint16_t kAds1115ConfigStartCh0 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0100 << 12;
static constexpr uint16_t kAds1115ConfigStartCh1 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0101 << 12;

// Buffer for DMA transactions.
static uint8_t data_buffer[5];

// We update the conversion timestamps at the timer tick handler
// to have more consistent timestamp intervals.
static uint32_t current_event_timestamp_millis = 0;
static uint32_t next_event_timestamp_millis = 0;

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
  // Started I2c operation 3 (start conversion of next channel)
  STATE_STEP3
};

static State state = STATE_UNDEFINED;

// The IRQ sequence sends these events to the processing task, each
// time a new conversion value is read from the ADC.
struct IrqEvent {
  uint32_t timestamp_millis;
  uint8_t ch;
  int16_t adc_value;
};

static StaticQueue<IrqEvent, 5> irq_event_queue;

// 0 -> ADC chan 0 (voltage)
// 1 -> ADC chan 1 (current)
static uint8_t channel = 0;

// Called before setup to test if the pw card is plugged in.
// Allows to support system with and without the pw card, during
// transition.
static bool does_hardware_exist() {
  if (state != STATE_UNDEFINED) {
    error_handler::Panic(311);
  }

  // Three attemps to read conversion register 0.
  for (int i = 0; i < 3; i++) {
    // Select ADC register 0 for reading (conversion data)
    static_assert(sizeof(data_buffer[0]) == 1);
    static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 1);
    data_buffer[0] = 0;
    static_assert(configTICK_RATE_HZ == 1000);
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        &hi2c1, kAds1115DeviceAddress, data_buffer, 1, 50);
    if (status != HAL_OK) {
      // Retry, just in case.
      continue;
    }
    // Read selected register
    static_assert(sizeof(data_buffer[0]) == 1);
    static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 2);
    data_buffer[0] = 0;
    data_buffer[1] = 0;
    static_assert(configTICK_RATE_HZ == 1000);
    status = HAL_I2C_Master_Receive(
        &hi2c1, kAds1115DeviceAddress, data_buffer, 2, 50);
    if (status == HAL_OK) {
      return true;
    }
  }
  return false;
}

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
    error_handler::Panic(213);
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
  const IrqEvent event = {.timestamp_millis = current_event_timestamp_millis,
                          .ch = channel,
                          .adc_value = (int16_t)reg_value};
  if (!irq_event_queue.add_from_isr(event, task_woken)) {
    // Comment this out for debugging with breakpoints
    error_handler::Panic(214);
  }
}
}  // namespace step2

// Hanlers for STEP3.
namespace step3 {
// Start conversion of the channel whose index is in 'channel'.
static inline void start_from_isr() {
  if (state != STATE_STEP2) {
    error_handler::Panic(219);
  }
  const uint16_t config_value =
      channel == 0 ? kAds1115ConfigStartCh0 : kAds1115ConfigStartCh1;
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
  switch (state) {
    case STATE_STEP1:
      step1::on_completion_from_isr();
      step2::start_from_isr();
      break;

    case STATE_STEP2: {
      BaseType_t task_woken = pdFALSE;
      step2::on_completion_from_isr(&task_woken);
      // Select next channel
      channel++;
      if (channel > 1) {
        channel = 0;
      }
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

static void setup() {
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

void pw_card_task_body(void* ignored_argument) {
  if (!does_hardware_exist()) {
    for (;;) {
      logger.warning("Heater card not found. Ignoring channel pw1.");
      time_util::delay_millis(3000);
    }
  }

  // Hardware found. Start the normal operation.
  setup();

  if (!pw_card_timer.start()) {
    error_handler::Panic(123);
  }

  // We allocate the buffer on demand.
  data_queue::DataBuffer* data_buffer = nullptr;
  SerialPacketsData* packet_data = nullptr;
  uint16_t items_in_buffer = 0;

  bool is_first_iteration = true;

  // Process the ADC reading. Each data point is a pair of readings, from
  // chan 0 and from chan 1, respectivly.
  for (;;) {
    // Get channel 0 value.
    IrqEvent event0;
    bool ok = irq_event_queue.consume_from_task(&event0, portMAX_DELAY);
    if (!ok) {
      error_handler::Panic(122);
    }
    if (event0.ch != 0) {
      error_handler::Panic(124);
    }

    // Get channel 1 value.
    IrqEvent event1;
    ok = irq_event_queue.consume_from_task(&event1, portMAX_DELAY);
    if (!ok) {
      error_handler::Panic(125);
    }
    if (event1.ch != 1) {
      error_handler::Panic(126);
    }

    // Drop value of first iteration since we read the first ADC value
    // before we start a conversion.
    if (is_first_iteration) {
      is_first_iteration = false;
      continue;
    }

    // If no bufer, allocate and fill in the headers. We can do it here
    // since we know the timestamp of the first data point.
    if (data_buffer == nullptr) {
      // Allocate new buffer.
      data_buffer = data_queue::grab_buffer();
      packet_data = &data_buffer->packet_data();
      items_in_buffer = 0;

      // Fill packet header.
      packet_data->clear();
      packet_data->write_uint8(1);               // packet version
      packet_data->write_uint32(session::id());  // Device session id.
      // We use the average of the two timestamps.
      const uint32_t start_time =
          (event0.timestamp_millis + event1.timestamp_millis) / 2;
      packet_data->write_uint32(start_time);  // Device session id.

      // Fill in the channgel header
      packet_data->write_uint8(0x30);                   // Channel id ('va1')
      packet_data->write_uint16(0);                     // Time offset.
      packet_data->write_uint16(kDataPointsPerPacket);  // Num points
      packet_data->write_uint16(kMsPerDataPoint);  // Interval between points.
    }

    // Add next data point.
    packet_data->write_uint16((uint16_t)event0.adc_value);
    packet_data->write_uint16((uint16_t)event1.adc_value);
    items_in_buffer++;

    // Handle a full buffer.
    if (items_in_buffer >= kDataPointsPerPacket) {
      // Relinquish the data buffer for queing.
      data_queue::queue_buffer(data_buffer);
      data_buffer = nullptr;
      packet_data = nullptr;
      items_in_buffer = 0;

      // Dump the last data point, for sanity check.
      logger.info("pw1 %hd, %hd", event0.adc_value, event1.adc_value);
    }
  }
}

// This function is called from the timer daemon and thus should be non
// blocking.
static void pw_card_timer_cb(TimerHandle_t xTimer) {
  // NOTE: Since we drop the first data point, we don't care about
  // the initial value of next_event_timestamp_millis.
  current_event_timestamp_millis = next_event_timestamp_millis;
  next_event_timestamp_millis = time_util::millis();

  step1::start_from_timer();
}

}  // namespace pw_card