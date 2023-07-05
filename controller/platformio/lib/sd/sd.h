#pragma once

#include "serial_packets_data.h"

namespace sd {



bool open_log_file(const char* name);

void append_to_log_file(const StuffedPacketBuffer& packet);

void close_log_file();

bool is_log_file_open_ok();





} // namespace sd