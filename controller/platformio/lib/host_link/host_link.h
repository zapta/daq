#pragma once

#include "serial.h"
#include "serial_packets_client.h"
#include "static_task.h"

// A callback handler for incoming host link commands. Implemented by 
// the controller.
// PacketStatus host_link_command_handler(uint8_t endpoint, const SerialPacketsData& command_data,
//                      SerialPacketsData& response_data);

// // A callback handler for incoming host link messages. Implemented by 
// // the controller.
// void host_link_message_handler(uint8_t endpoint, const SerialPacketsData& message_data);


namespace host_link {

enum HostPorts {
  LOG_REPORT_MESSAGE = 10
};

enum SelfPorts {
  CONTROL_COMMAND = 1
};

// Messages and commands can be sent to the host via this client.
extern SerialPacketsClient client;

// Main calls this once aupon initialization.
void setup(Serial& serial);

// Caller should provide a task to run this task body.
extern TaskBodyFunction host_link_task_body;
}  // namespace host_link