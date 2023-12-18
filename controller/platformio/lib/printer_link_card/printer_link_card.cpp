#include "printer_link_card.h"

#include "controller.h"
#include "data_recorder.h"
#include "logger.h"
#include "static_task.h"
#include "time_util.h"

namespace printer_link_card {

// Initialized in setup() to point to the serial port.
static Serial* printer_link_serial = nullptr;

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
// external report characters collected so far.
static controller::ExternalReportStr external_report_buffer;

static data_recorder::RecordingName new_recording_name_buffer;

static constexpr char kStartRecordingCommandPrefix[] = "cmd:start_recording:";
static constexpr size_t kStartRecordingCommandPrefixLen =
    sizeof(kStartRecordingCommandPrefix) - 1;

static constexpr char kStopRecordingCommand[] = "cmd:stop_recording";

// Perfomrs a state transitions. OK to reenter existing state.
static void set_state(State new_state) {
  state = new_state;
  external_report_buffer.clear();
  collect_start_millis = (new_state == COLLECT) ? time_util::millis() : 0;
  logger.info("Printer link: State -> %s",
              state == COLLECT ? "COLLECT" : "IDLE");
}

// One time initialization of this module. Should be the first
// function called.
void setup(Serial* serial) {
  if (printer_link_serial) {
    // Already initialized.
    error_handler::Panic(82);
  }
  set_state(IDLE);
  printer_link_serial = serial;
}

// report_str doesn't include the bounding '[', ']'.
static void handle_incoming_report(
    const controller::ExternalReportStr& report_str) {
  // If not a command, pass on to the logging.
  if (!report_str.starts_with("cmd:")) {
    controller::report_external_data(external_report_buffer);
    return;
  }

  // Handle the start test command.
  if (report_str.starts_with(kStartRecordingCommandPrefix)) {
    // Extract recording name.
    new_recording_name_buffer.set_c_str(report_str.c_str() +
                                        kStartRecordingCommandPrefixLen);

    // Validate recording name. Should not be empty or contain a ':' seperator.
    if (new_recording_name_buffer.is_empty() ||
        new_recording_name_buffer.find_char(
            ':', kStartRecordingCommandPrefixLen) != -1) {
      logger.error("Invalid start_recording cmd: [%s], ignoring.",
                   report_str.c_str());
      return;
    }

    const bool started_ok =
        data_recorder::start_recording(new_recording_name_buffer);
    if (!started_ok) {
      logger.error("%s failed to start recording for test [%s]",
                   kStartRecordingCommandPrefix,
                   new_recording_name_buffer.c_str());
    }
    return;
  }

  // Handle the stop test command.
  if (report_str.equals(kStopRecordingCommand)) {
    data_recorder::stop_recording();
    return;
  }

  // Here it's an orphan command.
  logger.error("Invalid command: [%s], ignoring.", report_str.c_str());
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
    if (external_report_buffer.is_empty()) {
      logger.error("Dropping an empty external report");
    } else {
      handle_incoming_report(external_report_buffer);
      // controller::report_external_data(external_report_buffer);
    }
    set_state(IDLE);
    return;
  }

  // Validate the char.
  if (!controller::is_valid_external_report_char(c)) {
    logger.error("Invalid external report char: [0x%02x]", c);
    set_state(IDLE);
    return;
  }

  // Append the char to the report string. Check for overflow.
  const bool ok = external_report_buffer.append(c);
  if (!ok) {
    logger.error("External report is too long %s...",
                 external_report_buffer.c_str());
    set_state(IDLE);
    return;
  }

  // Character added OK.
}

static void printer_link_task_body_impl(void* ignored_argument) {
  if (!printer_link_serial) {
    // Setup not called.
    error_handler::Panic(55);
  }
  for (;;) {
    // Wait for rx chars.
    const int n = printer_link_serial->read(temp_buffer, sizeof(temp_buffer));
    logger.info("Printer link: Recieved %d chars", n);
    // If current COLLECT session is too old, clear it.
    if (state == COLLECT) {
      const uint32_t millis_in_collect =
          time_util::millis() - collect_start_millis;
      if (millis_in_collect > 1000) {
        logger.error("External report RX timeout, dropping left overs: [%s...]",
                     external_report_buffer.c_str());
        set_state(IDLE);
      }
    }
    // Process the chars.
    for (int i = 0; i < n; i++) {
      process_next_rx_char(temp_buffer[i]);
    }
  }
}

// The exported task body.
TaskBodyFunction printer_link_task_body(printer_link_task_body_impl, nullptr);

}  // namespace printer_link_card