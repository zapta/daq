#pragma once

#include "serial.h"

namespace printer_link_card {

// Main calls this once aupon initialization with the
// serial port to use.
void setup(Serial* serial);

// Main allocates a task that executes this task body.
// Doesn't return.
void printer_link_task_body(void* ignored_argument);

}  // namespace printer_link_card