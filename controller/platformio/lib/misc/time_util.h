#pragma once

// A simple timer for measuring elapsed time in milliseconds.

#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

namespace time_util {
inline uint32_t millis() {
  // This should be optimized out by the compiler.
  if (configTICK_RATE_HZ != 1000) {
    Error_Handler();
  }
  return xTaskGetTickCount();
}
}  // namespace time_util


class Elappsed {
 public:
  Elappsed() { reset(); }

  void reset() { start_millis_ = time_util::millis(); }

  uint32_t elapsed_millis() { return time_util::millis() - start_millis_; }

  void set(uint32_t elapsed_millis) {
    start_millis_ = time_util::millis() - elapsed_millis;
  }

 private:
  uint32_t start_millis_;
};

