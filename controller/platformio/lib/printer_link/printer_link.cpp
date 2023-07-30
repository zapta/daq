#include "printer_link.h"

#include "logger.h"
#include "time_util.h"

namespace printer_link {

// Initialized in setup() to point to the serial port.
static Serial* link_serial = nullptr;

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