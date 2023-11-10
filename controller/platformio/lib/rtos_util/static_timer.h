#pragma once

#include <FreeRTOS.h>

#include "timers.h"
#include "common.h"


class StaticTimer {
 public:
  // User should call start() for the timer to start. pvTimerID is a user
  // id that is that is accessible by the callback via the xTimer argument.
  StaticTimer(TimerCallbackFunction_t timer_callback, const char* const name,
              void* pvTimerID)
      : _timer_callback(timer_callback), _name(name), _pv_timer_id(pvTimerID) {
  }

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
    _handle = xTimerCreateStatic(_name, period_millis, pdTRUE, _pv_timer_id,
                                 _timer_callback, &_tcb);
    const BaseType_t status = xTimerStart(_handle, 0);
    return status == pdPASS;
  }

 private:
  TimerCallbackFunction_t const _timer_callback;
  const char* const _name;
  void* const _pv_timer_id;
  StaticTimer_t _tcb;
  TimerHandle_t _handle = nullptr;
};
