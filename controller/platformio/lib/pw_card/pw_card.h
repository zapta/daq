#pragma once

#include <FreeRTOS.h>

#include "i2c_scheduler.h"
#include "static_task.h"

namespace pw_card {

// Tasks should start BEFORE devices.
extern TaskBody& i2c1_pw1_device_task_body;
extern I2cDevice& i2c1_pw1_device;

}  // namespace pw_card
