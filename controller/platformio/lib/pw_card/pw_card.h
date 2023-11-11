#pragma once

#include <FreeRTOS.h>

// #include "timers.h"
#include "i2c_scheduler.h"
#include "static_task.h"



namespace pw_card {

// Caller should provide a task to run this task body.
extern TaskBodyFunction pw_card_task_body;

extern I2cDevice& i2c1_pw1_device;

}  // namespace pw_card

