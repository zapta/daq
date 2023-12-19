#pragma once

#include "serial_packets_data.h"
#include "static_string.h"

namespace data_recorder {

typedef StaticString<30> RecordingName;

struct RecordingInfo {
  bool recording_active = false;
  // The following fields are valids IFF recording_active = true.
  RecordingName recording_name;
  uint32_t recording_start_time_millis = 0;
  uint32_t writes_ok = 0;
  uint32_t write_failures = 0;
};

// Stop the current recording, and start a new one.
bool start_recording(const RecordingName& new_recording_name);

// Stop existing recording, if any.
void stop_recording();

// Ignored silently if recording is off.
// Packet should be a serialized LOG packet with no write errors.
void append_log_record_if_recording(const SerialPacketsData& packet_data);

bool is_recording_active();

void get_recoding_info(RecordingInfo* state);

}  // namespace data_recorder