#pragma once

#include "serial.h"
#include "static_task.h"

namespace printer_link_card {

// Main calls this once aupon initialization with the
// serial port to use.
void setup(Serial* serial);

// Caller should provide a task to run this runnable.
extern StaticRunnable printer_link_task_runnable;

}  // namespace printer_link_card