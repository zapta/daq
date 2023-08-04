#include "data_recorder.h"

// #include <wchar.h>
// #include <array>

#include <cstring>

#include "fatfs.h"
#include "io.h"
#include "logger.h"
#include "static_mutex.h"
#include "time_util.h"

// extern SD_HandleTypeDef hsd1;

// Workaround per https://github.com/artlukm/STM32_FATFS_SDcard_remount
extern Disk_drvTypeDef disk;

namespace data_recorder {

// All vars are protected by the mutex.
StaticMutex mutex;

enum State {
  // Session off
  STATE_IDLE,
  // Session should be on
  // STATE_SESSION,
  STATE_MOUNTED,
  STATE_OPENED
};

static State state = STATE_IDLE;

// [July 2023] - Writing packets of arbitrary size resulted in
// occaionaly corrupted file with a few bytes added or missings
// throuout the file. As a workaround, we write to the SD only
// in chunks that are multiple of _MAX_SS (512).
static uint8_t
    write_buffer[serial_packets_consts::MAX_STUFFED_PACKET_LEN + _MAX_SS];
// Number of active pending bytes at the begining of write_buffer.
static uint32_t pending_bytes = 0;

// Session name + ".log" suffix.
constexpr uint32_t kMaxFileNameLen = RecordingName::kMaxLen + 4;

static RecordingName current_recording_name;

// Stats for diagnostics.
static uint32_t recording_start_time_millis = 0;
static uint32_t writes_ok = 0;
static uint32_t write_failures = 0;

// Assumes level == STATE_OPENED and mutex is grabbed.
// Tries to write pending bytes to SD. Always clears pending_bytes upon return.
// We call this function with pending_bytes being a multiple of _MAX_SS
// except for the last write in the file.
static void internal_write_all_pending_bytes() {
  if (pending_bytes == 0) {
    // Nothing to do.
    return;
  }

  const uint32_t n = pending_bytes;
  pending_bytes = 0;

  // This number should be a multipe of _MAX_SS.
  unsigned int bytes_written;
  FRESULT status = f_write(&SDFile, write_buffer, n, &bytes_written);
  if (status != FRESULT::FR_OK) {
    write_failures++;
    logger.error("Error writing to SD recording file, status=%d", status);
    return;
  }
  if (bytes_written != n) {
    write_failures++;
    logger.error("Requested to write to SD %lu bytes, %lu written", n,
                 (uint32_t)bytes_written);
    return;
  }

  status = f_sync(&SDFile);
  if (status != FRESULT::FR_OK) {
    write_failures++;
    logger.warning("Failed to flush SD file, status=%d", status);
  }

  writes_ok++;
}

// Grab mutex before calling this
// TODO: Change mutexs to be recursive.
static void internal_stop_recording() {
  if (state >= STATE_OPENED) {
    internal_write_all_pending_bytes();
    f_close(&SDFile);
  }

  if (state >= STATE_MOUNTED) {
    // Workaround per https://github.com/artlukm/STM32_FATFS_SDcard_remount
    // disk.is_initialized[SDFatFS.drv] = 0;

    // The 'NULL' cause to unmount.
    f_mount(&SDFatFS, (TCHAR const*)NULL, 0);
  }

  if (state == STATE_OPENED) {
    logger.info("Stopped recording [%s]", current_recording_name.c_str());
  }

  state = STATE_IDLE;
  pending_bytes = 0;
  writes_ok = 0;
  write_failures = 0;
  recording_start_time_millis = 0;
  current_recording_name.clear();
}

void stop_recording_session() {
  MutexScope scope(mutex);

  if (state == STATE_IDLE) {
    logger.info("No session to stop.");
  }

  // We call the internal stop anyway to make sure all
  // variables are clear.
  internal_stop_recording();
}

// Stops the current session, if any, and if new_session_name is not
// nullptr, tries to start a new session with given name.
bool start_recording(const RecordingName& new_session_name) {
  MutexScope scope(mutex);

  internal_stop_recording();

  if (!current_recording_name.set_c_str(new_session_name.c_str())) {
    // Should not happen since we have identical buffer sizes.
    App_Error_Handler();
  }

  FRESULT status = f_mount(&SDFatFS, (TCHAR const*)SDPath, 0);
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_mount failed. (FRESULT=%d)", status);
    internal_stop_recording();
    return false;
  }

  state = STATE_MOUNTED;

  // UTF16. Extra char for terminator. Temporary buffer for
  // opening the session recording file.
  static TCHAR recording_file_wname[kMaxFileNameLen + 1];

  static_assert(sizeof(recording_file_wname[0]) == 2U);
  static_assert(sizeof(recording_file_wname[0]) == sizeof(TCHAR));
  static_assert(
      (sizeof(recording_file_wname) / sizeof(recording_file_wname[0])) >=
      (RecordingName::kMaxLen + 5));

  const size_t n = new_session_name.len();
  const char* c_str = new_session_name.c_str();
  unsigned int i;
  for (i = 0; i < n; i++) {
    // Casting utf8 to utf16.
    recording_file_wname[i] = c_str[i];
  }
  recording_file_wname[i++] = '.';
  recording_file_wname[i++] = 'l';
  recording_file_wname[i++] = 'o';
  recording_file_wname[i++] = 'g';
  recording_file_wname[i++] = 0;

  // Not expecting buffer here overflow since we checked the sizes
  // above, but just in case.
  if (i > (sizeof(recording_file_wname) / sizeof(recording_file_wname[0]))) {
    App_Error_Handler();
  }

  status = f_open(&SDFile, recording_file_wname, FA_CREATE_ALWAYS | FA_WRITE);
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_open failed. (FRESULT=%d)", status);
    internal_stop_recording();
    return false;
  }

  state = STATE_OPENED;
  recording_start_time_millis = time_util::millis();
  logger.info("Started recording [%s]", current_recording_name.c_str());
  return true;
}

void append_if_recording(const StuffedPacketBuffer& packet) {
  MutexScope scope(mutex);

  if (packet.had_write_errors()) {
    logger.error("Trying to record a log record with write errors");
    return;
  }

  if (state == STATE_IDLE) {
    // Not recording. Ignore silently.
    return;
  }

  // Check state.
  if (state != STATE_OPENED) {
    write_failures ++;
    logger.error("Can't write to recorder file, (state=%d)", state);
    return;
  }

  // Determine packet size.
  const uint16_t packet_size = packet.size();
  if (packet_size == 0) {
    write_failures ++;
    logger.warning("Requested to write 0 bytes to SD.");
    return;
  }

  if (pending_bytes + packet_size > sizeof(write_buffer)) {
    // Should not happen since we derive buffer size from max packet size.
    App_Error_Handler();
  }

  // Split the packet into two parts, the number of bytes that will be
  // written now and the number of bytes that will stay pending for
  // a future write. We assume that pending bytes < _MAX_SS.
  const uint16_t total_bytes = pending_bytes + packet_size;
  const uint16_t packet_bytes_left_over = (total_bytes >= _MAX_SS) ?
      (total_bytes % _MAX_SS) : packet_size;
  const uint16_t packet_bytes_to_write = packet_size - packet_bytes_left_over;

  logger.info("pending=%lu. This packet: size: %hu, packet_write: %hu, packet_left over: %hu",
              pending_bytes, packet_size, packet_bytes_to_write, packet_bytes_left_over);


  // Maybe write pending and packet part 1.
  packet.reset_reading();
  if (packet_bytes_to_write) {
    // Append part 1 to the pending bytes.
    packet.read_bytes(&write_buffer[pending_bytes], packet_bytes_to_write);
    if (packet.had_read_errors()) {
      // Should not happen since we verified the size.
      App_Error_Handler();
    }
    pending_bytes += packet_bytes_to_write;

    // Write to SD.
    internal_write_all_pending_bytes();
  }

  // If there are left over bytes, store as pending bytes.
  if (packet_bytes_left_over) {
    packet.read_bytes(&write_buffer[pending_bytes], packet_bytes_left_over);
    if (packet.had_read_errors()) {
      // Should not happen since we derived from packet size.
      App_Error_Handler();
    }
    pending_bytes += packet_bytes_left_over;
  }

  if (!packet.all_read_ok()) {
    // Should not happen since we derived from packet size.
    App_Error_Handler();
  }
}

bool is_recording_active() {
  MutexScope scope(mutex);
  return (state != STATE_IDLE);
}

// void get_current_recording_name(RecordingName* name) {
//   name->set_c_str(current_recording_name.c_str());
// }

void get_recoding_info(RecordingInfo* info) {
  MutexScope scope(mutex);

  if (state != STATE_IDLE) {
    info->recording_active = true;
    info->recording_name.set_c_str(current_recording_name.c_str());
    info->recording_time_millis =
        time_util::millis() - recording_start_time_millis;
    info->writes_ok = writes_ok;
    info->write_failures = write_failures;
  } else {
    info->recording_active = false;
    info->recording_name.clear();
    info->recording_time_millis = 0;
    info->writes_ok = 0;
    info->write_failures = 0;
  }
}

}  // namespace data_recorder