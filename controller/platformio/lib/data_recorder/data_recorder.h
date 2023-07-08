#pragma once

#include "serial_packets_data.h"
#include "static_string.h"

namespace data_recorder {

typedef StaticString<30> RecordingName;



// Stop the current recording, and start a new one.
bool start_recording(const RecordingName& new_recording_name);

// Stop existing recording, if any.
void stop_recording_session();

// Ignored silently if recording is off.
// Packet should be a serialized LOG packet with no write errors.
void append_if_recording(const StuffedPacketBuffer& packet);


bool is_recording_active();

// Sets name with the current recording name or clears it 
// if recording is off.
 void get_current_recording_name(RecordingName* name);

// bool is_disk_inserted();

void dump_summary();






} // namespace data_recorder