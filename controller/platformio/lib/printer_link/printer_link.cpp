#include "printer_link.h"

#include "logger.h"
#include "time_util.h"
#include "controller.h"

namespace printer_link {

// Initialized in setup() to point to the serial port.
static Serial* link_serial = nullptr;

// NOTE: Since the rx task is the only task that access
// these variables, we don't need to protect with a mutex.

// static controller::EventName event_name_buffer;
// If event_name_buffer is not empty, contains  the 
// first char ex time in system millis. Undefined
// when event_name_buffer is empty.
// static uint32_t event_start_time_millies = 0;

// Main calls this once aupon initialization.
void setup(Serial& serial) {
  if (link_serial) {
    // Already initialized.
    Error_Handler();
  }
  link_serial = &serial;
}

void rx_task_body(void* argument) {
  if (!link_serial) {
    // Setup not called.
    Error_Handler();
  }
  for (;;) {
    logger.info("Printer link rx loop");
    time_util::delay_millis(1000);
  }
}

}  // namespace printer_link