#pragma once

#include "serial_packets_data.h"

namespace sd {

// FATFS without LFN is limited to 8.3 file names.
constexpr uint8_t kMaxSessionNameLen = 8;

bool start_session_log(const char* session_name);

void append_to_session_log(const StuffedPacketBuffer& packet);

void stop_session_log();

bool is_session_log_open_ok();
bool is_session_log_idle() ;

bool is_disk_inserted();






} // namespace sd