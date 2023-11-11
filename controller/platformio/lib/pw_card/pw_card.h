#pragma once

#include <FreeRTOS.h>

#include "i2c_scheduler.h"
#include "static_task.h"

namespace pw_card {

// Power devcie "pw1".
extern I2cDevice& i2c1_pw1_device;
extern TaskBody& i2c1_pw1_device_task_body;

}  // namespace pw_card
