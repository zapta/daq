#pragma once

#include "FreeRTOS.h"
#include "task.h"

namespace util {

// Return number of bytes in current task's stack that
// where never used.
uint32_t task_stack_unused_bytes();

}  // namespace util