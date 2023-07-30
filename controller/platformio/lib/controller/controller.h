#pragma once

#include "serial_packets_consts.h"
#include "serial_packets_data.h"
#include "static_string.h"

namespace controller {

typedef StaticString<30> EventName;

PacketStatus host_link_command_handler(uint8_t endpoint, const SerialPacketsData& command_data,
                     SerialPacketsData& response_data);

// A callback handler for incoming host link messages. Implemented by 
// the controller.
void host_link_message_handler(uint8_t endpoint, const SerialPacketsData& message_data);
// bool is_adc_report_enabled();
  
void report_event(const EventName& event_name);

}  // namespace controller