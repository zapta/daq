#pragma once

#include <FreeRTOS.h>

#include "timers.h"

namespace i2c_handler {

// Does not return.
void i2c_task_body(void* argument);

}  // namespace i2c_handler
