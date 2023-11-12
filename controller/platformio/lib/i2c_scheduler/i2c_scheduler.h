#pragma once

#include "i2c.h"
#include "main.h"
#include "static_timer.h"

class I2cDevice {
 public:
  I2cDevice() {}

  virtual void on_scheduler_init(I2C_HandleTypeDef* scheduler_hi2c,
                                  uint16_t slot_length_ms,
                                  uint16_t slot_internval_ms) = 0;
  virtual void on_i2c_slot_begin(uint32_t slot_sys_time_millis) = 0;
  virtual void on_i2c_complete_isr() = 0;
  virtual void on_i2c_error_isr() = 0;
  virtual bool is_i2c_bus_in_use() = 0;
};

struct I2cSlot {
  I2cDevice* const device;
};

// Describes the schedule of a single I2C bugs (e.g. i2c1).
// Each devices called every ms_per_slot * slotes_per_cycle ms.
struct I2cSchedule {
  static constexpr uint8_t kMaxSlotsperSycle = 10;
  // This controls the timer tick. Should be long enough such hat
  // all devices can complete their I2C transactions within that time.
  const uint16_t ms_per_slot;
  // The number of slots in each cycle. Not all slots have to be
  // active with actual devices.
  const uint8_t slots_per_cycle;
  const I2cSlot slots[kMaxSlotsperSycle];

  bool is_valid() MUST_USE_VALUE;
};

// Device scheduler for a single i2c channel (e.g. i2c1);
class I2cScheduler : public TimerCallback {
 public:
  I2cScheduler(I2C_HandleTypeDef* hi2c, const char* name)
      : _hi2c(hi2c), _name(name), _timer(*this, name) {}

  // Prevent copy and assignment.
  I2cScheduler(const I2cScheduler& other) = delete;
  I2cScheduler& operator=(const I2cScheduler& other) = delete;

  bool start(I2cSchedule* schedule) MUST_USE_VALUE;

 private:
  I2C_HandleTypeDef* const _hi2c;
  const char* _name;
  StaticTimer _timer;
  I2cSchedule* _schedule = nullptr;
  uint8_t _slot_index_in_cycle = 0;

  // The timer calls this method on each tick.
  // void timer_callback(TimerHandle_t xTimer);

  // ISR handlers that are shared by all schedulers.
  static void i2c_shared_completion_isr(I2C_HandleTypeDef* hi2c);
  static void i2c_shared_error_isr(I2C_HandleTypeDef* hi2c);

  // Maps hi2c to scheduler. Panic if not found.
  static inline I2cScheduler* isr_hi2c_to_scheduler(
      const I2C_HandleTypeDef* hi2c);

  // Called on each timer tick.
  void timer_callback();

  // Called from i2c isrs.
  void on_i2c_completion_isr();
  void on_i2c_error_isr();
};

namespace i2c_scheduler {
// Scheduler for I2C1 channel.
extern I2cScheduler i2c1_scheduler;
}  // namespace i2c_scheduler
