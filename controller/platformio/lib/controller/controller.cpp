#include "controller.h"

#include "host_link.h"
#include "serial_packets_client.h"
#include "static_mutex.h"

namespace controller {

// Protect access to prot_vars.
static StaticMutex mutex;

// struct ProtVars {
//   // bool adc_report_enabled = false;
// };

// static ProtVars prot_vars;

// bool is_adc_report_enabled() {
//   MutexScope scope(mutex);
//   return prot_vars.adc_report_enabled;
// }

PacketStatus handle_control_command(const SerialPacketsData& command_data,
                                    SerialPacketsData& response_data) {
  // Assuming command_data reading is reset and response data is empty.
  const uint8_t op_code = command_data.read_uint8();
  if (command_data.had_read_errors()) {
    logger.error("Error reading control command code");
    return PacketStatus::INVALID_ARGUMENT;
  }
  switch (op_code) {
    // Command 0x01 - NOP. Command data should be empty.
    case 0x01:
      return command_data.all_read_ok() ? PacketStatus::OK
                                        : PacketStatus::INVALID_ARGUMENT;
      break;

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
      logger.error("Unknown control command code %hx", op_code);
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