#pragma once

#include <FreeRTOS.h>

#include "common.h"
#include "timers.h"

// Abstraction of a timer callback.
class TimerCallback {
 public:
  virtual void timer_callback() = 0;
};

// typedef void foo(void) TimerCallback_t;

// Implementation of TimerCallback using a static function.
class TimerCallbackFunction : public TimerCallback {
 public:
  typedef void (*TimerCbFunction_t)(void);

  TimerCallbackFunction(TimerCbFunction_t timer_function)
      : _timer_function(timer_function) {}
  virtual void timer_callback() { _timer_function(); }

 private:
  TimerCbFunction_t const _timer_function;
  // void* const _pvParameters;
};

class StaticTimer {
 public:
  // User should call start() for the timer to start.
  StaticTimer(TimerCallback& timer_callback, const char* const name)
      : _timer_callback(timer_callback), _name(name) {}

  // This will typically be used in testing only.
  ~StaticTimer() {
    const BaseType_t status = xTimerDelete(_handle, 0);
    if (status != pdPASS) {
      error_handler::Panic(121);
    }
  }

  // Prevent copy and assignment.
  StaticTimer(const StaticTimer& other) = delete;
  StaticTimer& operator=(const StaticTimer& other) = delete;

  // Should be called for the timer to start.
  inline bool start(uint32_t period_millis) MUST_USE_VALUE {
    if (_handle) {
      return false;  // Already started.
    }

    static_assert(configTICK_RATE_HZ == 1000);
    // We pass this StaticTimer as the user data of the RTOS timer.
    _handle = xTimerCreateStatic(_name, period_millis, pdTRUE, this,
                                 timer_callback_dispatcher, &_tcb);
    const BaseType_t status = xTimerStart(_handle, 0);
    return status == pdPASS;
  }

 private:
  TimerCallback& _timer_callback;
  const char* const _name;
  // void* const _pv_timer_id;
  StaticTimer_t _tcb;
  TimerHandle_t _handle = nullptr;

  // A shared FreeRTOS timer callback that dispatches to the TimerCallback.
  static void timer_callback_dispatcher(TimerHandle_t xTimer) {
    StaticTimer* const static_timer = (StaticTimer*)pvTimerGetTimerID(xTimer);
    // Not expected to return.
    (static_timer->_timer_callback).timer_callback();
  }
};
