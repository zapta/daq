#include "controller.h"

#include "host_link.h"
#include "sd.h"
#include "serial_packets_client.h"
#include "static_mutex.h"

namespace controller {

// TODO: Allow longer file name in SD module.
constexpr uint8_t kMaxSessionIdLen = 8;

// Protect access to prot_vars.
static StaticMutex mutex;

struct ProtVars {
  char tmp_session_id[kMaxSessionIdLen + 1] = {0};
};

static ProtVars prot_vars;

// bool is_adc_report_enabled() {
//   MutexScope scope(mutex);
//   return prot_vars.adc_report_enabled;
// }

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
      const uint8_t session_id_len = command_data.read_uint8();
      if (session_id_len == 0 || session_id_len > kMaxSessionIdLen) {
        logger.error("START command: Invalid session id length: %hu.",
                     session_id_len);
        return PacketStatus::LENGTH_ERROR;
      }
      command_data.read_bytes((uint8_t*)&prot_vars.tmp_session_id,
                              session_id_len);
      if (!command_data.all_read_ok()) {
        logger.error("START command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      const bool had_session = !sd::is_log_file_idle();
      prot_vars.tmp_session_id[session_id_len] = 0;
      // Does nothing if not opened.
      sd::close_log_file();
      const bool opened_ok = sd::open_log_file(prot_vars.tmp_session_id);
      if (!opened_ok) {
        logger.error("START command: failed to create a SD log file %s",
                     prot_vars.tmp_session_id);
        return PacketStatus::GENERAL_ERROR;
      }
      response_data.write_uint8(had_session? 1 : 0);
      logger.info("START command: Started a new session: %s", prot_vars.tmp_session_id);
      return PacketStatus::OK;
    } break;

    // Command 0x03 - Stop currently running session. If nay..
    case 0x03: {
      MutexScope scope(mutex);
      // Get session id string.
      // const uint8_t session_id_len = command_data.read_uint8();
      // if (session_id_len == 0 || session_id_len > kMaxSessionIdLen) {
      //   logger.error("SART command has invalid session id length: %hu.",
      //                session_id_len);
      //   return PacketStatus::LENGTH_ERROR;
      // }
      // command_data.read_bytes((uint8_t*)&prot_vars.tmp_session_id,
      //                         session_id_len);
      if (!command_data.all_read_ok()) {
        logger.error("STOP command: Invalid command data.");
        return PacketStatus::INVALID_ARGUMENT;
      }
      const bool had_session = !sd::is_log_file_idle();
      // prot_vars.tmp_session_id[session_id_len] = 0;
      // Does nothing if not opened.
      sd::close_log_file();
      // const bool opened_ok = sd::open_log_file(prot_vars.tmp_session_id);
      // if (!opened_ok) {
      //   logger.error("Failed to create a SD log file %s",
      //                prot_vars.tmp_session_id);
      //   return PacketStatus::GENERAL_ERROR;
      // }
      response_data.write_uint8(had_session? 1 : 0);
      logger.info(had_session? "STOP command: Session stopped." : "STOP command: No session to stop");
      return PacketStatus::OK;
    } break;

      // Command 0x01 - Set/Reset adc report messaging.
      // Command data:
      //   uint8_t:  0x00 -> off
      //             0x01 -> on
      //             else -> error
      // Response data:
      //   empty
      // case 0x02: {
      //   const uint8_t flag = command_data.read_uint8();
      //   if (!command_data.all_read_ok() || (flag > 1)) {
      //     logger.error("Error in control command 0x02 (%hu, %02hu)",
      //                  command_data.size(), flag);
      //     return PacketStatus::INVALID_ARGUMENT;
      //   }
      //   // Update the flag within a critical section.
      //   {
      //   MutexScope scope(mutex);
      //   prot_vars.adc_report_enabled = flag != 0;
      //   }
      //   logger.info("Set adc reporting to %hd", flag);
      //   return PacketStatus::OK;

      // } break;

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