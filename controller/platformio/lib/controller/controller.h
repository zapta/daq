#pragma once

#include <ctype.h>

#include "serial_packets_consts.h"
#include "serial_packets_data.h"
#include "static_string.h"

namespace controller {

typedef StaticString<30> MarkerName;

PacketStatus host_link_command_handler(uint8_t endpoint,
                                       const SerialPacketsData& command_data,
                                       SerialPacketsData& response_data);

// A callback handler for incoming host link messages. Implemented by
// the controller.
void host_link_message_handler(uint8_t endpoint,
                               const SerialPacketsData& message_data);

void report_marker(const MarkerName& marker_name);

inline bool is_valid_marker_char(uint8_t c) {
  return isalnum(c) || strchr("-_.:", c);
}

// void report_log_data(const SerialPacketsData& data);

}  // namespace controller