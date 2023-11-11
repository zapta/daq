#include "pw_card.h"

#include <FreeRtos.h>
#include <i2c.h>

#include "common.h"
#include "data_queue.h"
#include "error_handler.h"
#include "session.h"
#include "static_queue.h"
// #include "static_timer.h"
#include "time_util.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

// namespace pw_card {

// kAds1115DeviceAddress

// The IRQ sequence sends these events to the processing task, each
// time a new conversion value is read from the ADC.
struct IrqEvent {
  uint32_t timestamp_millis;
  uint8_t ch;
  int16_t adc_value;
};

// I2c device implementation for the ADS1115B ADC.
class I2cPwDevice : public I2cDevice, public TaskBody {
 public:
  I2cPwDevice(I2C_HandleTypeDef* hi2c, uint8_t device_address,
              const char* chan_id)
      : _hi2c(hi2c), _i2c_device_address(device_address), _pw_chan_id(chan_id) {}

  // Prevent copy and assignment.
  I2cPwDevice(const I2cPwDevice& other) = delete;
  I2cPwDevice& operator=(const I2cPwDevice& other) = delete;

  // Methods of I2cDevice
  virtual void on_i2c_slot_timer(uint32_t slot_sys_time_millis);
  virtual void on_i2c_complete_isr();
  virtual void on_i2c_error_isr();

 private:
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

  // The I2C channel.
  I2C_HandleTypeDef* const _hi2c;
  // The defice address on the I2c bus. E.g.  0x48 << 1
  const uint8_t _i2c_device_address;
  const char* _pw_chan_id;
  // The current ADC channel we process. Either 0 or 1.
  uint8_t _current_adc_channel_index = 0;
  uint8_t _dma_data_buffer[4] = {0};
  uint32_t _current_event_timestamp_millis = 0;
  uint32_t _next_event_timestamp_millis = 0;
  StaticQueue<IrqEvent, 5> _irq_event_queue;
  State _state = STATE_UNDEFINED;

  // const uint8_t _device_address;
  void step1_start_from_timer();
  void step1_on_completion_from_isr();
  void step2_start_from_isr();
  void step2_on_completion_from_isr(BaseType_t* task_woken);
  void step3_start_from_isr();
  void step3_on_completion_from_isr();

  // Implemenation of TaskBody parent
  void task_body();
};

// static void pw_card_timer_cb(TimerHandle_t xTimer);

// Each data point contains a pair of readings for voltage
// and current respectivly.
static constexpr uint16_t kDataPointsPerPacket = 8;
static constexpr uint16_t kMsPerTimerTick = 25;
static constexpr uint16_t kMsPerDataPoint = 2 * kMsPerTimerTick;

// Timer with static allocation. 25ms interval, for 20 data points per
// seconds (each data points includes two ADC readings)
// static StaticTimer pw_card_timer(pw_card::pw_card_timer_cb, "PW", nullptr);

// static constexpr uint8_t kAds1115DeviceAddress = 0x48 << 1;

// Sampling time 1/128 sec. 4.096V full scale. Single mode.
static constexpr uint16_t kAds1115BaseConfig = 0b0000001110000000;
static constexpr uint16_t kAds1115ConfigStartCh0 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0100 << 12;
static constexpr uint16_t kAds1115ConfigStartCh1 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0101 << 12;

// Buffer for DMA transactions.
// static uint8_t data_buffer[5];

// We update the conversion timestamps at the timer tick handler
// to have more consistent timestamp intervals.
// static uint32_t current_event_timestamp_millis = 0;
// static uint32_t next_event_timestamp_millis = 0;

// Module states.
// enum State {
//   // Initial state, before setup.
//   STATE_UNDEFINED,
//   // Ready for next sampling cycle.
//   STATE_IDLE,
//   // Started I2C operation 1 (select register 0 to read)
//   STATE_STEP1,
//   // Started I2C operation 2 (read ADC value from selected register.)
//   STATE_STEP2,
//   // Started I2c operation 3 (start conversion of next channel)
//   STATE_STEP3
// };

// static State state = STATE_UNDEFINED;

// static StaticQueue<IrqEvent, 5> irq_event_queue;

// 0 -> ADC chan 0 (voltage)
// 1 -> ADC chan 1 (current)
// static uint8_t channel = 0;

// TODO: Figure out how to implement this under the I2C scheduler.
//
// Called before setup to test if the pw card is plugged in.
// Allows to support system with and without the pw card, during
// transition.
// static bool does_hardware_exist() {
//   if (state != STATE_UNDEFINED) {
//     error_handler::Panic(311);
//   }

//   // Three attemps to read conversion register 0.
//   for (int i = 0; i < 3; i++) {
//     // Select ADC register 0 for reading (conversion data)
//     static_assert(sizeof(data_buffer[0]) == 1);
//     static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 1);
//     data_buffer[0] = 0;
//     static_assert(configTICK_RATE_HZ == 1000);
//     HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
//         &hi2c1, kAds1115DeviceAddress, data_buffer, 1, 50);
//     if (status != HAL_OK) {
//       // Retry, just in case.
//       continue;
//     }
//     // Read selected register
//     static_assert(sizeof(data_buffer[0]) == 1);
//     static_assert(sizeof(data_buffer) / sizeof(data_buffer[0]) >= 2);
//     data_buffer[0] = 0;
//     data_buffer[1] = 0;
//     static_assert(configTICK_RATE_HZ == 1000);
//     status = HAL_I2C_Master_Receive(&hi2c1, kAds1115DeviceAddress,
//     data_buffer,
//                                     2, 50);
//     if (status == HAL_OK) {
//       return true;
//     }
//   }
//   return false;
// }

// Should start before the i2c scheduler.
void I2cPwDevice::task_body() {
  if (_state != STATE_UNDEFINED) {
    error_handler::Panic(119);
  }

  // TODO: Figure out how to implement this under the I2C scheduler.
  //
  // if (!does_hardware_exist()) {
  //   for (;;) {
  //     logger.warning("Heater card not found. Ignoring channel pw1.");
  //     time_util::delay_millis(3000);
  //   }
  // }

  // Hardware found. Start the normal operation.
  // setup();

  // if (!pw_card_timer.start(kMsPerTimerTick)) {
  //   error_handler::Panic(123);
  // }

  // We allocate the buffer on demand.
  data_queue::DataBuffer* data_buffer = nullptr;
  SerialPacketsData* packet_data = nullptr;
  uint16_t items_in_buffer = 0;

  bool is_first_iteration = true;

  _state = STATE_IDLE;

  // Process the ADC reading. Each data point is a pair of readings, from
  // chan 0 and from chan 1, respectivly.
  for (;;) {
    // Get channel 0 value.
    IrqEvent event0;
    bool ok = _irq_event_queue.consume_from_task(&event0, portMAX_DELAY);
    if (!ok) {
      error_handler::Panic(122);
    }
    if (event0.ch != 0) {
      error_handler::Panic(124);
    }

    // Get channel 1 value.
    IrqEvent event1;
    ok = _irq_event_queue.consume_from_task(&event1, portMAX_DELAY);
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
      packet_data->write_str(_pw_chan_id);
      packet_data->write_uint16(0);  // Time offset from packet start time.
      packet_data->write_uint16(
          kDataPointsPerPacket);  // Num of data points we plan to add
      packet_data->write_uint16(kMsPerDataPoint);  // Interval between points.
    }

    // Add next data point.
    packet_data->write_uint16((uint16_t)event0.adc_value);
    packet_data->write_uint16((uint16_t)event1.adc_value);
    items_in_buffer++;

    // If buffer has enough items, queue it for sending.
    if (items_in_buffer >= kDataPointsPerPacket) {
      // Relinquish the data buffer for queing.
      data_queue::queue_buffer(data_buffer);
      data_buffer = nullptr;
      packet_data = nullptr;
      items_in_buffer = 0;

      // Dump the last data point, for sanity check.
      logger.info("%s %hd, %hd", _pw_chan_id, event0.adc_value, event1.adc_value);
    }
  }
}

// This function is called from the timer daemon and thus should be non
// blocking.
// static void pw_card_timer_cb(TimerHandle_t xTimer) {
//   // NOTE: Since we drop the first data point, we don't care about
//   // the initial value of next_event_timestamp_millis.
//   current_event_timestamp_millis = next_event_timestamp_millis;
//   next_event_timestamp_millis = time_util::millis();

//   step1::start_from_timer();
// }

void I2cPwDevice::on_i2c_slot_timer(uint32_t slot_sys_time_millis) {
  // Track slot timestamp
  _current_event_timestamp_millis = _next_event_timestamp_millis;
  _next_event_timestamp_millis = slot_sys_time_millis;

  // Start the dma sequences.
  step1_start_from_timer();
}

void I2cPwDevice::on_i2c_complete_isr() {
  switch (_state) {
    case STATE_STEP1:
      step1_on_completion_from_isr();
      step2_start_from_isr();
      break;

    case STATE_STEP2: {
      BaseType_t task_woken = pdFALSE;
      step2_on_completion_from_isr(&task_woken);
      // Select next channel
      _current_adc_channel_index++;
      if (_current_adc_channel_index > 1) {
        _current_adc_channel_index = 0;
      }
      step3_start_from_isr();
      // In case the queue push above requires a task switch.
      portYIELD_FROM_ISR(task_woken)

    } break;

    case STATE_STEP3:
      // Cycle completed OK.
      // state = STATE_IDLE;
      step3_on_completion_from_isr();
      break;

    default:
      error_handler::Panic(211);
  }
}

inline void I2cPwDevice::on_i2c_error_isr() { error_handler::Panic(117); }

inline void I2cPwDevice::step1_start_from_timer() {
  if (_state != STATE_IDLE) {
    error_handler::Panic(216);
  }
  static_assert(sizeof(_dma_data_buffer[0]) == 1);
  static_assert(sizeof(_dma_data_buffer) / sizeof(_dma_data_buffer[0]) >= 1);
  _dma_data_buffer[0] = 0;
  _state = STATE_STEP1;
  HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, _i2c_device_address, _dma_data_buffer, 1);
  if (status != HAL_OK) {
    error_handler::Panic(213);
  }
}

inline void I2cPwDevice::step1_on_completion_from_isr() {
  // Nothing to do here
}

void I2cPwDevice::step2_start_from_isr() {
  if (_state != STATE_STEP1) {
    error_handler::Panic(217);
  }
  // Start reading the conversion value from the selected register 0.
  static_assert(sizeof(_dma_data_buffer[0]) == 1);
  static_assert(sizeof(_dma_data_buffer) / sizeof(_dma_data_buffer[0]) >= 2);
  _dma_data_buffer[0] = 0;
  _dma_data_buffer[1] = 0;
  _state = STATE_STEP2;
  const HAL_StatusTypeDef status = HAL_I2C_Master_Receive_DMA(
      &hi2c1, _i2c_device_address, _dma_data_buffer, 2);
  if (status != HAL_OK) {
    error_handler::Panic(212);
  }
}

inline void I2cPwDevice::step2_on_completion_from_isr(BaseType_t* task_woken) {
  // static void on_completion_from_isr(BaseType_t* task_woken) {
  if (_state != STATE_STEP2) {
    error_handler::Panic(218);
  }
  // Here when completed to read the conversion value from reg 0.
  // Use the value conversion value.
  const uint16_t reg_value = ((uint16_t)_dma_data_buffer[0] << 8) | _dma_data_buffer[1];
  const IrqEvent event = {.timestamp_millis = _current_event_timestamp_millis,
                          .ch = _current_adc_channel_index,
                          .adc_value = (int16_t)reg_value};
  if (!_irq_event_queue.add_from_isr(event, task_woken)) {
    // Comment this out for debugging with breakpoints
    error_handler::Panic(214);
  }
}

// Start conversion of the channel whose index is in 'channel'.
inline void I2cPwDevice::step3_start_from_isr() {
  if (_state != STATE_STEP2) {
    error_handler::Panic(219);
  }
  const uint16_t config_value =
      _current_adc_channel_index == 0 ? kAds1115ConfigStartCh0 : kAds1115ConfigStartCh1;
  static_assert(sizeof(_dma_data_buffer[0]) == 1);
  static_assert(sizeof(_dma_data_buffer) / sizeof(_dma_data_buffer[0]) >= 3);
  _dma_data_buffer[0] = 0x01;  // config reg address
  _dma_data_buffer[1] = (uint8_t)(config_value >> 8);
  _dma_data_buffer[2] = (uint8_t)config_value;
  _state = STATE_STEP3;
  const HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, _i2c_device_address, _dma_data_buffer, 3);
  if (status != HAL_OK) {
    error_handler::Panic(215);
  }
}

inline void I2cPwDevice::step3_on_completion_from_isr() {
  if (_state != STATE_STEP3) {
    error_handler::Panic(221);
  }
  // I2C transaction sequence completed.
  _state = STATE_IDLE;
}

namespace pw_card {

// The the device and task body as references to the base classes.
static I2cPwDevice _i2c1_pw1_device(&hi2c1, 0x48 << 1, "pw1");
TaskBody& i2c1_pw1_device_task_body = _i2c1_pw1_device;
I2cDevice& i2c1_pw1_device = _i2c1_pw1_device;

}  // namespace pw_card