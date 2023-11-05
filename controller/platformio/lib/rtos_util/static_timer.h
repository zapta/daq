#pragma once

#include <FreeRTOS.h>

#include "error_handler.h"
#include "timers.h"

class StaticTimer {
 public:
  // User should call start() for the timer to start.
  StaticTimer(TimerCallbackFunction_t timer_callback, const char* const name,
              uint32_t period_millis)
      : _handle(xTimerCreateStatic(name, period_millis, pdTRUE, this,
                                   timer_callback, &_tcb)) {
    static_assert(configTICK_RATE_HZ == 1000);
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
  inline bool start() {
    const BaseType_t status = xTimerStart(_handle, 0);
    return status == pdPASS;
  }

 private:
  StaticTimer_t _tcb;
  TimerHandle_t const _handle;
};
