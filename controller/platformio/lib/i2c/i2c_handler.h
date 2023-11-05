#pragma once

#include <FreeRTOS.h>

#include "timers.h"

namespace i2c_handler {

// Should be called once to initialize.
void setup();

// Does not return.
void i2c_task_body(void* argument);

// Called from a freertos timer. Should be non blocking.
void i2c_timer_cb(TimerHandle_t xTimer);

}  // namespace i2c_handler
