#pragma once

// A simple timer for measuring elapsed time in milliseconds.

#include "FreeRTOS.h"
#include "task.h"

// Assuming that FreeRTOS tick time is 1ms.

class Elappsed {
 public:
  Elappsed() { reset(); }

  void reset() { start_millis_ = xTaskGetTickCount(); }

  uint32_t elapsed_millis() { return xTaskGetTickCount() - start_millis_; }

  void set(uint32_t elapsed_millis) {
    start_millis_ = xTaskGetTickCount() - elapsed_millis;
  }

 private:
  uint32_t start_millis_;
};

namespace time_util {
inline uint32_t millis() { return xTaskGetTickCount(); }
}  // namespace time_util