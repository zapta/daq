#include "printer_link.h"

#include <ctype.h>

#include "controller.h"
#include "logger.h"
#include "time_util.h"

namespace printer_link {

// Initialized in setup() to point to the serial port.
static Serial* link_serial = nullptr;

// NOTE: Since the rx task is the only task that access
// these variables, we don't need to protect with a mutex.

enum State { IDLE, COLLECT };
static State state;

// Temp buffer for reading rx chars. Lengnth doesn't matter much.
static uint8_t temp_buffer[40];

// Valid in COLLECT mode, zero otherwise. Contains the time COLLECT
// state was entered.
static uint32_t collect_start_millis;

// Valid in COLLECT mode, empty otherwise. Contains the
// event name characters collected so far.
static controller::EventName event_name_buffer;

// Perfomrs a state transitions. OK to reenter existing state.
static void set_state(State new_state) {
  state = new_state;
  event_name_buffer.clear();
  collect_start_millis = (new_state == COLLECT) ? time_util::millis() : 0;
  logger.info("State -> %s", state == COLLECT ? "COLLECT" : "IDLE");
}

// One time initialization of this module. Should be the first
// function called.
void setup(Serial& serial) {
  if (link_serial) {
    // Already initialized.
    Error_Handler();
  }
  set_state(IDLE);
  link_serial = &serial;
}

static inline bool is_valid_event_char(uint8_t c) {
  return isalnum(c) || strchr("-_.:", c);
}

static void process_next_rx_char(uint8_t c) {
  // In IDLE state we wait for next start char.
  if (state == IDLE) {
    if (c == '[') {
      set_state(COLLECT);
    } else {
      logger.error("Dropping orphan char: [0x%02x]", c);
    }
    return;
  }

  // Here we are in collect mode.
  //
  // Handle the end char.
  if (c == ']') {
    controller::report_event(event_name_buffer);
    set_state(IDLE);
    return;
  }

  // Validate event name char.
  if (!is_valid_event_char(c)) {
    logger.error("Invalid event name char: [0x%02x]", c);
    set_state(IDLE);
    return;
  }

  // Append the char to event name. Check for overflow.
  const bool ok = event_name_buffer.append(c);
  if (!ok) {
    logger.error("Event name too long %s...", event_name_buffer.c_str());
    set_state(IDLE);
    return;
  }

  // Character added OK.
}

void rx_task_body(void* argument) {
  if (!link_serial) {
    // Setup not called.
    Error_Handler();
  }
  for (;;) {
    // Wait for rx chars.
    const int n = link_serial->read(temp_buffer, sizeof(temp_buffer));
    logger.info("Recieved %d chars", n);
    // If current COLLECT session is too old, clear it.
    if (state == COLLECT) {
      const uint32_t millis_in_collect =
          time_util::millis() - collect_start_millis;
      if (millis_in_collect > 1000) {
        logger.error("Event name RX timeout, dropping left overs: [%s]",
                     event_name_buffer.c_str());
        set_state(IDLE);
      }
    }
    // Process the chars.
    for (int i = 0; i < n; i++) {
      process_next_rx_char(temp_buffer[i]);
    }
  }
}

}  // namespace printer_link