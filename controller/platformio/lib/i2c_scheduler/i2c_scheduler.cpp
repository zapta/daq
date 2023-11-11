
#include "i2c_scheduler.h"

#include "common.h"
#include "time_util.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

namespace i2c_scheduler {
I2cScheduler i2c1_scheduler(&hi2c1, "I2C1");
}

bool I2cSchedule::is_valid() {
  // ms_per_slot should be non zero
  if (ms_per_slot == 0) {
    return false;
  }

  // slots_per_cycle should be in [1, kMaxSlotsperSycle]
  if (slots_per_cycle == 0 || slots_per_cycle > kMaxSlotsperSycle) {
    return false;
  }

  // All usused slots should have no device.
  for (uint8_t i = slots_per_cycle; i < kMaxSlotsperSycle; i++) {
    if (slots[i].device != nullptr) {
      return false;
    }
  }

  return true;
}

// Start the scheduling of the I2C slots.
bool I2cScheduler::start(I2cSchedule* schedule) {
  if (!schedule->is_valid()) {
    return false;
  }

  _schedule = schedule;

  // The preincrement will cause the first tick to process slot zero.
  _slot_index_in_cycle = schedule->slots_per_cycle - 1;


  // Register the shared ISR handlers. Note that we share the same 
  // handler for transmit and recieve completion, and for error and 
  // abort.
   if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1,
                                         HAL_I2C_MASTER_TX_COMPLETE_CB_ID,
                                         i2c_shared_completion_isr)) {
    error_handler::Panic(111);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1,
                                         HAL_I2C_MASTER_RX_COMPLETE_CB_ID,
                                         i2c_shared_completion_isr)) {
    error_handler::Panic(112);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1, HAL_I2C_ERROR_CB_ID,
                                         i2c_shared_error_isr)) {
    error_handler::Panic(113);
  }
  if (HAL_OK != HAL_I2C_RegisterCallback(&hi2c1, HAL_I2C_ABORT_CB_ID,
                                         i2c_shared_error_isr)) {
    error_handler::Panic(114);
  }

  // Start the timer. This starts to send ticks to the devices.
  if (!_timer.start(_schedule->ms_per_slot)) {
    return false;
  }

  return true;
}

// This implements the method of the TimerCallback parent class.
void I2cScheduler::timer_callback() {
  // Take a time snapshot as close as possible to the
  // begining of the tick to have deterministic intervals.
  uint32_t slot_sys_time_millis = time_util::millis();

  // Increment the slot index.
  // We keep the slot index stable throughout the slot
  // since it's used also by isrs. Since the timer handler
  // and the ISRs operate at different times, we don't
  // need a mutex.
  _slot_index_in_cycle++;
  if (_slot_index_in_cycle >= _schedule->slots_per_cycle) {
    _slot_index_in_cycle = 0;
  }

  // Do nothing if the slot is not active.
  I2cDevice* const dev = _schedule->slots[_slot_index_in_cycle].device;
  if (!dev) {
    return;
  }

  // Call the start method of the device. This typically triggers
  // one or more I2C DMA/IT transfers that should complete before the
  // end of the slot, freeing the bus to the next device.
  dev->on_i2c_slot_timer(slot_sys_time_millis);
}

// This method is called from a timer so should not block.
// It is static and common to all i2c schedulers.
// void I2cScheduler::i2c_shared_scheduler_timer_cb(TimerHandle_t xTimer) {
//   // Get the timer user id. It's a pointer to the target I2cScheduler.
//   I2cScheduler* scheduler = (I2cScheduler*)pvTimerGetTimerID(xTimer);
//   if (!scheduler) {
//     error_handler::Panic(129);
//   }
//   scheduler->on_timer_slot_tick();
// }

// Called from ISR to map the hi2c to a scheduler.
inline I2cScheduler* I2cScheduler::isr_hi2c_to_scheduler(const I2C_HandleTypeDef* hi2c) {
  if (hi2c == &hi2c1) {
    return &i2c_scheduler::i2c1_scheduler;
  }
  error_handler::Panic(132);
}


// Dispatch the shared ISR to the sepcific scheduler.
void I2cScheduler::i2c_shared_completion_isr(I2C_HandleTypeDef* hi2c) {
  I2cScheduler* const scheduler = isr_hi2c_to_scheduler(hi2c);
  scheduler->on_i2c_completion_isr();
}

void I2cScheduler::i2c_shared_error_isr(I2C_HandleTypeDef* hi2c) {
  I2cScheduler* const scheduler = isr_hi2c_to_scheduler(hi2c);
  scheduler->on_i2c_error_isr();
}

// Scheduler specifoc completion isr.
void I2cScheduler::on_i2c_completion_isr() {
  I2cDevice* const dev = _schedule->slots[_slot_index_in_cycle].device;
  if (!dev) {
    error_handler::Panic(133);
  }
  dev -> on_i2c_complete_isr();
}

// Scheduler specifoc error and abort isr.
void I2cScheduler::on_i2c_error_isr() {
  I2cDevice* const dev = _schedule->slots[_slot_index_in_cycle].device;
  if (!dev) {
    error_handler::Panic(134);
  }
  dev -> on_i2c_error_isr();
}
