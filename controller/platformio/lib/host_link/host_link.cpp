#include "host_link.h"

#include "serial_packets_client.h"
#include "controller.h"

namespace host_link {

SerialPacketsClient client;

void setup(Serial& serial) {
  // The command and message handler are implemented by the controller.
  // client.begin(serial, host_link_command_handler, host_link_message_handler);
  client.begin(serial, controller::host_link_command_handler, controller::host_link_message_handler);
}

void host_link_task_body(void* ignored_argument) {
  // This method doesn't return.
  client.rx_task_body();
  error_handler::Panic(81);
}

}  // namespace host_link