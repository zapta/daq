#pragma once

#include "serial.h"

namespace printer_link {

// Main calls this once aupon initialization with the
// serial port to use.
void setup(Serial& serial);

// Main allocates a task that executes this task body.
// Doesn't return.
void rx_task_body(void* argument);

}  // namespace printer_link