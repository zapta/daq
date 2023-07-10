#include "controller.h"

#include "data_recorder.h"
#include "host_link.h"
#include "serial_packets_client.h"
#include "static_mutex.h"

namespace controller {


// Protects access to the variables below.
static StaticMutex mutex;

static data_recorder::RecordingName tmp_new_log_session_name;
static data_recorder::RecordingInfo tmp_recording_info;


PacketStatus handle_control_command(const SerialPacketsData& command_data,
                                    SerialPacketsData& response_data) {
  // Assuming command_data reading is reset and response data is empty.
  const uint8_t op_code = command_data.read_uint8();
  if (command_data.had_read_errors()) {
    logger.error("COMMAND: error reading command code.");
    return PacketStatus::INVALID_ARGUMENT;
  }
  switch (op_code) {
    // Command 0x01 - NOP. Command data should be empty.
    case 0x01:
      if (!command_data.all_read_ok()) {
        logger.error("NOP command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      return PacketStatus::OK;
      break;

    // Command 0x02 - START a new session with given name.
    case 0x02: {
      MutexScope scope(mutex);
      // Get session id string.
      command_data.read_str(&tmp_new_log_session_name);
      if (!command_data.all_read_ok()) {
        logger.error("START command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      const bool had_old_session = data_recorder::is_recording_active();
      const bool opened_ok =
          data_recorder::start_recording(tmp_new_log_session_name);
      if (!opened_ok) {
        logger.error(
            "START command: failed to create recording file for session [%s]",
            tmp_new_log_session_name.c_str());
        return PacketStatus::GENERAL_ERROR;
      }
      response_data.write_uint8(had_old_session ? 1 : 0);
      return PacketStatus::OK;
    } break;

    // Command 0x03 - Stop currently running session. If nay..
    case 0x03: {
      MutexScope scope(mutex);
      if (!command_data.all_read_ok()) {
        logger.error("STOP command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      const bool had_old_session = data_recorder::is_recording_active();
      data_recorder::stop_recording_session();
      response_data.write_uint8(had_old_session ? 1 : 0);
      return PacketStatus::OK;
    } break;

    // Command 0x04 - Get status
    case 0x04: {
      MutexScope scope(mutex);
      if (!command_data.all_read_ok()) {
        logger.error("STATUS command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      response_data.write_uint8(1);  // Format version
      data_recorder::get_recoding_info(&tmp_recording_info);
      if (tmp_recording_info.recording_active) {
        response_data.write_uint8(1);
        response_data.write_uint32(tmp_recording_info.recording_time_millis);
        response_data.write_str(tmp_recording_info.recording_name.c_str());
        response_data.write_uint32(tmp_recording_info.writes_ok);
        response_data.write_uint32(tmp_recording_info.write_failures);
      } else {
        response_data.write_uint8(0);
      }
      return PacketStatus::OK;
    } break;

    default:
      logger.error("COMMAND: Unknown command code %hx", op_code);
      return PacketStatus::INVALID_ARGUMENT;
  }

  return PacketStatus::UNHANDLED;
}

}  // namespace controller

PacketStatus host_link_command_handler(uint8_t endpoint,
                                       const SerialPacketsData& command_data,
                                       SerialPacketsData& response_data) {
  if (endpoint == host_link::SelfPorts::CONTROL_COMMAND) {
    logger.info("Recieved a control commannd at endpoint %02hx", endpoint);
    return controller::handle_control_command(command_data, response_data);
  }
  logger.error("Ignored command at endpoint %02hx", endpoint);
  return PacketStatus::UNHANDLED;
}

// A callback type for incoming messages.
void host_link_message_handler(uint8_t endpoint,
                               const SerialPacketsData& message_data) {
  logger.info("Recieved a message at endpoint %02hx", endpoint);
}