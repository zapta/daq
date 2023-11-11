#pragma once

#include "serial.h"
#include "static_task.h"

namespace printer_link_card {

// Main calls this once aupon initialization with the
// serial port to use.
void setup(Serial* serial);

// Caller should provide a task to run this task body.
// Should be started after setup().
extern TaskBodyFunction printer_link_task_body;

}  // namespace printer_link_card