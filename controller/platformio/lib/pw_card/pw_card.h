#pragma once

#include <FreeRTOS.h>

#include "timers.h"

namespace pw_card {

// Does not return.
void pw_card_task_body(void* ignored_argument);

}  // namespace pw_card
