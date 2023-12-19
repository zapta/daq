#include "controller.h"

#include "data_queue.h"
#include "data_recorder.h"
#include "gpio_pins.h"
#include "host_link.h"
#include "serial_packets_client.h"
#include "session.h"
#include "static_mutex.h"

using host_link::HostPorts;

namespace controller {

// Protects access to the variables below.
static StaticMutex mutex;

static data_recorder::RecordingName new_recording_name_buffer;
static data_recorder::RecordingInfo recording_info_buffer;

// Temp packet data. Used to encode log packets.
// static SerialPacketsData packet_data;

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

    // Command 0x02 - START a new recording with given name.
    case 0x02: {
      MutexScope scope(mutex);
      // Get recording id string.
      command_data.read_str(&new_recording_name_buffer);
      if (!command_data.all_read_ok()) {
        logger.error("START command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      const bool had_old_recording = data_recorder::is_recording_active();
      const bool started_ok =
          data_recorder::start_recording(new_recording_name_buffer);
      if (!started_ok) {
        logger.error("START command: failed to create recording file for [%s]",
                     new_recording_name_buffer.c_str());
        return PacketStatus::GENERAL_ERROR;
      }
      response_data.write_uint8(had_old_recording ? 1 : 0);
      return PacketStatus::OK;
    } break;

    // Command 0x03 - Stop currently running recording. If nay..
    case 0x03: {
      MutexScope scope(mutex);
      if (!command_data.all_read_ok()) {
        logger.error("STOP command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      const bool had_old_recording = data_recorder::is_recording_active();
      data_recorder::stop_recording();
      response_data.write_uint8(had_old_recording ? 1 : 0);
      return PacketStatus::OK;
    } break;

    // Command 0x04 - Get status
    case 0x04: {
      MutexScope scope(mutex);
      if (!command_data.all_read_ok()) {
        logger.error("STATUS command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      // Device info.
      response_data.write_uint8(1);                     // Format version
      response_data.write_uint32(session::id());        // Device session id.
      response_data.write_uint32(time_util::millis());  // Device time
      // SD card presense.
      response_data.write_uint8(gpio_pins::SD_SWITCH.is_high() ? 1 : 0);
      // Recording info.
      data_recorder::get_recoding_info(&recording_info_buffer);
      response_data.write_uint8(recording_info_buffer.recording_active ? 1 : 0);
      if (recording_info_buffer.recording_active) {
        response_data.write_uint32(recording_info_buffer.recording_start_time_millis);
        response_data.write_str(recording_info_buffer.recording_name.c_str());
        response_data.write_uint32(recording_info_buffer.writes_ok);
        response_data.write_uint32(recording_info_buffer.write_failures);
      }
      return PacketStatus::OK;
    } break;

    default:
      logger.error("COMMAND: Unknown command code %hx", op_code);
      return PacketStatus::INVALID_ARGUMENT;
  }

  return PacketStatus::UNHANDLED;
}

PacketStatus host_link_command_handler(uint8_t endpoint,
                                       const SerialPacketsData& command_data,
                                       SerialPacketsData& response_data) {
  if (endpoint == host_link::SelfPorts::CONTROL_COMMAND) {
    logger.info("Recieved a control commannd at endpoint %02hx", endpoint);
    return handle_control_command(command_data, response_data);
  }
  logger.error("Ignored command at endpoint %02hx", endpoint);
  return PacketStatus::UNHANDLED;
}

// A callback type for incoming messages.
void host_link_message_handler(uint8_t endpoint,
                               const SerialPacketsData& message_data) {
  // Currently we don't use nor expect incoming messages.
  logger.warning("Recieved a message at endpoint %02hx", endpoint);
}

// Report string is assumed to be validated.
void report_external_data(const ExternalReportStr& report_str) {
  MutexScope scope(mutex);

  // Encode a log record that contain 'ext' channel with
  // a single external data report
  //
  // NOTE: As of Dec 2023, the rate of external report is very low so we don't
  // mind sending each report in a packet of its own.
  {
    // Do not use buffer after it was queued.
    data_queue::DataBuffer* data_buffer = data_queue::grab_buffer();
    SerialPacketsData* packet_data = &data_buffer->packet_data();

    packet_data->clear();
    packet_data->write_uint8(1);                     // packet format version
    packet_data->write_uint32(session::id());        // Device session id.
    packet_data->write_uint32(time_util::millis());  // Base time.
    packet_data->write_str("ext");                   // External report meta channel id
    packet_data->write_uint16(0);                    // Relative time offset
    packet_data->write_uint16(1);                    // Num data points
    packet_data->write_str(report_str.c_str());

    // Verify writing was OK.
    if (packet_data->had_write_errors()) {
      error_handler::Panic(77);
    }

    // Report to monitor and maybe to SD.
    // Do not access buffer after this point.
    data_queue::queue_buffer(data_buffer);
    data_buffer = nullptr;
    packet_data = nullptr;
  }
  // TODO: Implementing logging and reporting in status.
  logger.info("Report: [%s]", report_str.c_str());
}

}  // namespace controller