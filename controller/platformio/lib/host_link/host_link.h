#pragma once

#include "serial.h"
#include "serial_packets_client.h"

// A callback handler for incoming host link commands. Implemented by 
// the controller.
PacketStatus host_link_command_handler(uint8_t endpoint, const SerialPacketsData& command_data,
                     SerialPacketsData& response_data);

// A callback handler for incoming host link messages. Implemented by 
// the controller.
void host_link_message_handler(uint8_t endpoint, const SerialPacketsData& message_data);


namespace host_link {

enum HostPorts {
  ADC_REPORT_MESSAGE = 10
};

enum SelfPorts {
  CONTROL_COMMAND = 1
};

// Messages and commands can be sent to the host via this client.
extern SerialPacketsClient client;

// Main calls this once aupon initialization.
void setup(Serial& serial);

// Main allocates a task that executes this task body.
// Doesn't return.
void rx_task_body(void* argument);

}  // namespace host_link