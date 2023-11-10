#pragma once

#include <FreeRTOS.h>

#include "timers.h"
#include "i2c_scheduler.h"



namespace pw_card {

// Does not return.
void pw_card_task_body(void* ignored_argument);

extern I2cDevice& i2c1_pw1_device;

}  // namespace pw_card

