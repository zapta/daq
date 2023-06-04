// A simple timer for measuring elapsed time in milliseconds.
#pragma once

#include "FreeRTOS.h"
#include "task.h"

// #include <Arduino.h>

class SerialPacketsTimer {
 public:
  SerialPacketsTimer() { reset(); }

  void reset() { start_millis_ = xTaskGetTickCount(); }

  uint32_t elapsed_millis() { return xTaskGetTickCount() - start_millis_; }

  void set(uint32_t elapsed_millis) {
    start_millis_ = xTaskGetTickCount() - elapsed_millis;
  }

 private:
  uint32_t start_millis_;
};