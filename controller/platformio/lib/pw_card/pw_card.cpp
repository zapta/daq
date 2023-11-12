#include "pw_card.h"

#include <FreeRtos.h>
#include <i2c.h>

#include "common.h"
#include "data_queue.h"
#include "error_handler.h"
#include "session.h"
#include "static_queue.h"
#include "time_util.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

// The number of data points we send in a log packet. Each
// data point contains a pair of voltage and current readings.
static constexpr uint16_t kDataPointsPerPacket = 8;

// ADS115B configuration.
// Sampling time 1/128 sec. 4.096V full scale. Single mode.
static constexpr uint16_t kAds1115BaseConfig = 0b0000001110000000;
static constexpr uint16_t kAds1115ConfigStartCh0 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0100 << 12;
static constexpr uint16_t kAds1115ConfigStartCh1 =
    kAds1115BaseConfig | 0b1 << 15 | 0b0101 << 12;

enum AdcChan {
  // RMS voltage reading.
  ADC_CHAN0,
  // RMS current reading.
  ADC_CHAN1
};

// An event that is sent to the task via the event queue. Uses
// to syncrhonize between tasks and ISRs.
enum EventType {
  HARDWARE_STATUS,
  ADC_READING,
};

struct AdcReading {
  uint32_t timestamp_millis;
  AdcChan chan;
  int16_t value;
};

struct Event {
  EventType type;
  union {
    // For HARDWARE_STATUS
    bool hardware_exists;
    // For ADC_READING
    AdcReading adc_reading;
  };  // values;
};

// I2c device implementation for the ADS1115B ADC.
class I2cPwDevice : public I2cDevice, public TaskBody {
 public:
  I2cPwDevice(I2C_HandleTypeDef* hi2c, uint8_t device_address,
              const char* pw_chan_id)
      : _hi2c(hi2c),
        _i2c_device_address(device_address),
        _pw_chan_id(pw_chan_id) {}

  // Prevent copy and assignment.
  I2cPwDevice(const I2cPwDevice& other) = delete;
  I2cPwDevice& operator=(const I2cPwDevice& other) = delete;

  // Methods of I2cDevice
  virtual void on_scheduler_start(I2C_HandleTypeDef* scheduler_hi2c,
                                  uint16_t slot_length_ms,
                                  uint16_t slot_internval_ms);
  virtual void on_i2c_slot_timer(uint32_t slot_sys_time_millis);
  virtual void on_i2c_complete_isr();
  virtual void on_i2c_error_isr();

 private:
  // Module states.
  enum State {
    // Initial state. on_scheduler_start() transition to
    // STATE_SCHEDULER_STARTED.
    STATE_UNDEFINED,
    // Scheduler started. on_on_i2c_slot_timer() calls
    STATE_SCHEDULER_STARTED,
    // Writing selected reg address.
    STATE_HARDWARE_TESTING_STEP1,
    // Reading selected reg.
    STATE_HARDWARE_TESTING_STEP2,
    //
    STATE_HARDWARE_TESTING_COMPLETED,
    // Everything OK and DMA is IDLE to start the normal ADC readings.
    STATE_ADC_READY,
    // Started I2C operation 1 (select register 0 to read)
    STATE_ADC_STEP1,
    // Started I2C operation 2 (read ADC value from selected register.)
    STATE_ADC_STEP2,
    // Started I2c operation 3 (start conversion of next channel)
    STATE_ADC_STEP3
  };

  // The I2C channel.
  I2C_HandleTypeDef* const _hi2c;
  // The defice address on the I2c bus. E.g.  0x48 << 1
  const uint8_t _i2c_device_address;
  const char* _pw_chan_id;
  // The current ADC channel we process. Either 0 or 1.
  AdcChan _current_adc_channel = ADC_CHAN0;
  uint8_t _dma_data_buffer[4] = {0};
  uint32_t _prev_slot_timestamp_millis = 0;
  uint32_t _current_slot_timestamp_millis = 0;
  StaticQueue<Event, 5> _event_queue;
  State _state = STATE_UNDEFINED;
  // Set by on_scheduler_start()
  uint16_t _data_point_internval_ms = 0;

  // Handlers for hardware testing steps.
  // void hardware_testing_step1_start_from_timer();
  // void hardware_testing_step1_completion_from_timer();
  // void hardware_testing_step2_start_from_isr();
  // void hardware_testing_step2_completion_from_is();

  // Handler for ADC reading.
  void adc_step1_start_from_timer();
  void adc_step1_on_completion_from_isr();
  void adc_step2_start_from_isr();
  void adc_step2_on_completion_from_isr(BaseType_t* task_woken);
  void adc_step3_start_from_isr();
  void adc_step3_on_completion_from_isr();

  // Implemenation of TaskBody parent
  void task_body();
};

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
  // Hardware testing in progress.
  if (_state >= STATE_ADC_READY) {
    error_handler::Panic(119);
  }

  // Wait for the hardware status event.
  Event event;
  bool ok = _event_queue.consume_from_task(&event, 3000);
  if (!ok) {
    error_handler::Panic(138);
  }
  if (event.type != EventType::HARDWARE_STATUS) {
    error_handler::Panic(139);
  }
  if (_state != STATE_HARDWARE_TESTING_COMPLETED) {
    error_handler::Panic(142);
  }

  // If hardware not found, do not advance STATE_ADC_READY.
  if (!event.hardware_exists) {
    for (;;) {
      logger.warning("%s card not found, igoring channel.", _pw_chan_id);
      time_util::delay_millis(3000);
    }
  }

  // Here when hardware found.
  _state = State::STATE_ADC_READY;

  // Track the log data buffer. We allocate it upon demand, wher we
  // know the timestamp of the first data point in the buffer.
  data_queue::DataBuffer* data_buffer = nullptr;
  SerialPacketsData* packet_data = nullptr;
  uint16_t items_in_buffer = 0;

  bool is_first_data_point = true;

  // Process the ADC reading. Each data point is a pair of readings, from
  // chan 0 and from chan 1, respectivly.
  for (;;) {
    // Get channel 0 value.
    Event event0;
    bool ok = _event_queue.consume_from_task(&event0, portMAX_DELAY);
    if (!ok) {
      error_handler::Panic(122);
    }
    if (event0.type != ADC_READING || event0.adc_reading.chan != ADC_CHAN0) {
      error_handler::Panic(124);
    }

    // Get channel 1 value.
    Event event1;
    ok = _event_queue.consume_from_task(&event1, portMAX_DELAY);
    if (!ok) {
      error_handler::Panic(125);
    }
    if (event1.type != ADC_READING || event1.adc_reading.chan != ADC_CHAN1) {
      error_handler::Panic(126);
    }

    // Drop value of first iteration since we read the first ADC value
    // before we start a conversion.
    if (is_first_data_point) {
      is_first_data_point = false;
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
      const uint32_t start_time = (event0.adc_reading.timestamp_millis +
                                   event1.adc_reading.timestamp_millis) /
                                  2;
      packet_data->write_uint32(start_time);  // Device session id.

      // Fill in the channgel header
      packet_data->write_str(_pw_chan_id);
      packet_data->write_uint16(0);  // Time offset from packet start time.
      packet_data->write_uint16(
          kDataPointsPerPacket);  // Num of data points we plan to add
      packet_data->write_uint16(
          _data_point_internval_ms);  // Interval between points.
    }

    // Add next data point.
    packet_data->write_uint16((uint16_t)event0.adc_reading.value);
    packet_data->write_uint16((uint16_t)event1.adc_reading.value);
    items_in_buffer++;

    // If buffer has enough items, queue it for sending.
    if (items_in_buffer >= kDataPointsPerPacket) {
      // Relinquish the data buffer for queing.
      data_queue::queue_buffer(data_buffer);
      data_buffer = nullptr;
      packet_data = nullptr;
      items_in_buffer = 0;

      // Dump the last data point, for sanity check.
      logger.info("%s %hd, %hd", _pw_chan_id, event0.adc_reading.value,
                  event1.adc_reading.value);
    }
  }
}

// This is called before the first tick.
void I2cPwDevice::on_scheduler_start(I2C_HandleTypeDef* _scheduler_hi2c,
                                     uint16_t slot_length_ms,
                                     uint16_t slot_internval_ms) {
  if (_scheduler_hi2c != _hi2c) {
    error_handler::Panic(141);
  }
  if (_state != STATE_UNDEFINED) {
    error_handler::Panic(136);
  }

  // Since each data point takes a slot for reading the voltage
  // and a slot to read the current, the data point rate is half
  // of that slot rate.
  _data_point_internval_ms = 2 * slot_internval_ms;

  // We expect at least 2ms reserved time per slot and data rate of 10Hz.
  if (slot_length_ms < 2 || _data_point_internval_ms > 100) {
    error_handler::Panic(135);
  }

  // Tell the task the scheduler is ready.
  // const Event event = {.type = EventType::SCHEDULER_STARTED};
  // if (!_irq_event_queue.add_from_task(event, 0)) {
  //   error_handler::Panic(137);
  // }

  // From here, the ticks will do the hardware testing.
  _state = STATE_SCHEDULER_STARTED;
}

void I2cPwDevice::on_i2c_slot_timer(uint32_t slot_sys_time_millis) {
  // Do nothing if still initializing.
  // if (_state == STATE_UNDEFINED) {
  //   return;
  // }

  // Track slot timestamps
  // TODO: Add a sanity check that the slot intervals are as expected.
  _prev_slot_timestamp_millis = _current_slot_timestamp_millis;
  _current_slot_timestamp_millis = slot_sys_time_millis;

  switch (_state) {
    // TODO: Impelement the hardware testing sequence.
    case State::STATE_SCHEDULER_STARTED: {
      _state = State::STATE_HARDWARE_TESTING_COMPLETED;
      const Event event = {.type = EventType::HARDWARE_STATUS,
                           {.hardware_exists = true}};
      if (!_event_queue.add_from_task(event, 0)) {
        error_handler::Panic(144);
      }
    } break;

    case State::STATE_ADC_READY:
      adc_step1_start_from_timer();
      break;

    default:
      error_handler::Panic(143);
  }
}

void I2cPwDevice::on_i2c_complete_isr() {
  switch (_state) {
    case STATE_ADC_STEP1:
      adc_step1_on_completion_from_isr();
      adc_step2_start_from_isr();
      break;

    case STATE_ADC_STEP2: {
      BaseType_t task_woken = pdFALSE;
      adc_step2_on_completion_from_isr(&task_woken);
      // Select next channel
      _current_adc_channel =
          (_current_adc_channel == ADC_CHAN0) ? ADC_CHAN1 : ADC_CHAN0;
      adc_step3_start_from_isr();
      // In case the queue push above requires a task switch.
      portYIELD_FROM_ISR(task_woken)

    } break;

    case STATE_ADC_STEP3:
      // Cycle completed OK.
      adc_step3_on_completion_from_isr();
      _state = STATE_ADC_READY;
      break;

    default:
      error_handler::Panic(211);
  }
}

inline void I2cPwDevice::on_i2c_error_isr() { error_handler::Panic(117); }

inline void I2cPwDevice::adc_step1_start_from_timer() {
  if (_state != STATE_ADC_READY) {
    error_handler::Panic(216);
  }
  static_assert(sizeof(_dma_data_buffer[0]) == 1);
  static_assert(sizeof(_dma_data_buffer) / sizeof(_dma_data_buffer[0]) >= 1);
  _dma_data_buffer[0] = 0;
  _state = STATE_ADC_STEP1;
  HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, _i2c_device_address, _dma_data_buffer, 1);
  if (status != HAL_OK) {
    error_handler::Panic(213);
  }
}

inline void I2cPwDevice::adc_step1_on_completion_from_isr() {
  // Nothing to do here
}

void I2cPwDevice::adc_step2_start_from_isr() {
  if (_state != STATE_ADC_STEP1) {
    error_handler::Panic(217);
  }
  // Start reading the conversion value from the selected register 0.
  static_assert(sizeof(_dma_data_buffer[0]) == 1);
  static_assert(sizeof(_dma_data_buffer) / sizeof(_dma_data_buffer[0]) >= 2);
  _dma_data_buffer[0] = 0;
  _dma_data_buffer[1] = 0;
  _state = STATE_ADC_STEP2;
  const HAL_StatusTypeDef status = HAL_I2C_Master_Receive_DMA(
      &hi2c1, _i2c_device_address, _dma_data_buffer, 2);
  if (status != HAL_OK) {
    error_handler::Panic(212);
  }
}

inline void I2cPwDevice::adc_step2_on_completion_from_isr(
    BaseType_t* task_woken) {
  // static void on_completion_from_isr(BaseType_t* task_woken) {
  if (_state != STATE_ADC_STEP2) {
    error_handler::Panic(218);
  }
  // Here when completed to read the conversion value from reg 0.
  // Use the value conversion value.
  const uint16_t ads_reg_value =
      ((uint16_t)_dma_data_buffer[0] << 8) | _dma_data_buffer[1];
  // We use the timestamp of previous slot since this is when we
  // started the conversion.
  const Event event = {
      .type = ADC_READING,
      {.adc_reading = {.timestamp_millis = _prev_slot_timestamp_millis,
                       .chan = _current_adc_channel,
                       .value = (int16_t)ads_reg_value}}};
  if (!_event_queue.add_from_isr(event, task_woken)) {
    // Comment this out for debugging with breakpoints
    error_handler::Panic(214);
  }
}

// Start conversion of the channel whose index is in 'channel'.
inline void I2cPwDevice::adc_step3_start_from_isr() {
  if (_state != STATE_ADC_STEP2) {
    error_handler::Panic(219);
  }
  const uint16_t config_value = (_current_adc_channel == ADC_CHAN0)
                                    ? kAds1115ConfigStartCh0
                                    : kAds1115ConfigStartCh1;
  static_assert(sizeof(_dma_data_buffer[0]) == 1);
  static_assert(sizeof(_dma_data_buffer) / sizeof(_dma_data_buffer[0]) >= 3);
  _dma_data_buffer[0] = 0x01;  // config reg address
  _dma_data_buffer[1] = (uint8_t)(config_value >> 8);
  _dma_data_buffer[2] = (uint8_t)config_value;
  _state = STATE_ADC_STEP3;
  const HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, _i2c_device_address, _dma_data_buffer, 3);
  if (status != HAL_OK) {
    error_handler::Panic(215);
  }
}

inline void I2cPwDevice::adc_step3_on_completion_from_isr() {
  // Nothing to do here.
}

namespace pw_card {

// The the device and task body as references to the base classes.
static I2cPwDevice _i2c1_pw1_device(&hi2c1, 0x48 << 1, "pw1");
I2cDevice& i2c1_pw1_device = _i2c1_pw1_device;
TaskBody& i2c1_pw1_device_task_body = _i2c1_pw1_device;

}  // namespace pw_card