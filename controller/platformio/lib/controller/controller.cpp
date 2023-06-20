#include "controller.h"

#include "host_link.h"
#include "serial_packets_client.h"
#include "static_mutex.h"

namespace controller {

// Protect access to prot_vars.
static StaticMutex mutex;

struct ProtVars {
  bool adc_report_enabled = false;
};

static ProtVars prot_vars;

bool is_adc_report_enabled() {
  MutexScope scope(mutex);
  return prot_vars.adc_report_enabled;
}

PacketStatus handle_control_command(const SerialPacketsData& command_data,
                                    SerialPacketsData& response_data) {
  // if (command_data.size() < 2) {
  //   return PacketStatus::INVALID_ARGUMENT;
  // }
  return PacketStatus::UNHANDLED;
}

}  // namespace controller

PacketStatus host_link_command_handler(uint8_t endpoint,
                                       const SerialPacketsData& command_data,
                                       SerialPacketsData& response_data) {
  logger.info("Recieved a command at endpoint %02hx", endpoint);
  if (endpoint == host_link::SelfPorts::CONTROL_COMMAND) {
    return controller::handle_control_command(command_data, response_data);
  }
  return PacketStatus::UNHANDLED;
}

// A callback type for incoming messages.
void host_link_message_handler(uint8_t endpoint,
                               const SerialPacketsData& message_data) {
  logger.info("Recieved a message at endpoint %02hx", endpoint);
}