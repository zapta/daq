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

// Stats for diagnostics
static uint32_t records_written = 0;

// Session name + ".log" suffix.
constexpr uint32_t kMaxFileNameLen = RecordingName::kMaxLen + 4;

// Extra char for the terminators. Valid when state
// is not IDLE.
// static char current_session_name[kMaxSessionNameLen + 1] = {0};

static RecordingName current_session_name;

// Assumes levle == STATE_OPENED and mutex is grabbed.
// Tries to write pending bytes to SD.
// Always clears pending_bytes upon return.
static void internal_write_pending_bytes() {
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
    logger.error("Error writing to SD recording file, status=%d", status);
    return;
  }
  if (bytes_written != n) {
    logger.error("Requested to write to SD %lu bytes, %lu written", n,
                 (uint32_t)bytes_written);
    return;
  }

  status = f_sync(&SDFile);
  if (status != FRESULT::FR_OK) {
    logger.warning("Failed to flush SD file, status=%d", status);
  }
}

// Grab mutex before calling this
// TODO: Change mutexs to be recursive.
static void internal_stop_recording() {
  if (state >= STATE_OPENED) {
    internal_write_pending_bytes();
    f_close(&SDFile);
  }

  if (state >= STATE_MOUNTED) {
    // Workaround per https://github.com/artlukm/STM32_FATFS_SDcard_remount
    // disk.is_initialized[SDFatFS.drv] = 0;

    // The 'NULL' cause to unmount.
    f_mount(&SDFatFS, (TCHAR const*)NULL, 0);
  }

  if (state == STATE_OPENED) {
    logger.info("Stopped recording [%s]", current_session_name.c_str());
  }

  state = STATE_IDLE;
  pending_bytes = 0;
  records_written = 0;
  current_session_name.clear();
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

  // if (new_session_name.is_empty()) {
  //   return true;
  // }

  // const size_t n = strlen(new_session_name);
  // if (n > kMaxSessionNameLen) {
  //   logger.error("Session name too long (%u). Can't start.", n);
  //   return false;
  // }

  // static_assert(kMaxSessionNameLen + 1 >= sizeof(current_session_name));
  // static_assert(current_session_name.kMaxLen >=  kMaxSessionNameLen);
  if (!current_session_name.set_c_str(new_session_name.c_str())) {
    // Should not happen since we verified the size.
    Error_Handler();
  }
  // strcpy(current_session_name, new_session_name);
  // if (!current_session_name.set())

  // // Constuct the UTF16 file name. Allow for '.log' extension.
  // if (n + 4 > kMaxFileNameLen) {
  //   logger.error("Session name too long. Can't start. (2).");
  //   return false;
  // }
  // static_assert(sizeof(tmp_log_file_wname)/sizeof(tmp_log_file_wname[0]) >=
  // kMaxFileNameLen);

  // pending_bytes = 0;
  // records_written = 0;

  //
  // state = STATE_MOUNTED;

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
  static_assert((sizeof(recording_file_wname) / sizeof(recording_file_wname[0])) >=
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
    Error_Handler();
  }

  status = f_open(&SDFile, recording_file_wname, FA_CREATE_ALWAYS | FA_WRITE);
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_open failed. (FRESULT=%d)", status);
    internal_stop_recording();
    return false;
  }
  state = STATE_OPENED;
  logger.info("Started recording [%s]", current_session_name.c_str());

  return true;
}

// Does nothing if already closed.
// void stop_session_log() {
//   MutexScope scope(mutex);

//   internal_close_log_file();
// }

void append_if_recording(const StuffedPacketBuffer& packet) {
  MutexScope scope(mutex);

  if (packet.had_write_errors()) {
    logger.error("Trying to record a log record with write errors");
    return;
  }

  // if (records_written >= kMaxRecordsToWrite) {
  //   logger.info("Aleady writen %lu records", kMaxRecordsToWrite);
  //   return;
  // }

  if (state == STATE_IDLE) {
    // Not in session. Ignore silently.
    return;
  }

  // Check state.
  if (state != STATE_OPENED) {
    logger.error("Can't write to recorder file, (state=%d)", state);
    return;
  }

  // Determine packet size.
  const uint16_t packet_size = packet.size();
  if (packet_size == 0) {
    logger.warning("Requested to write 0 bytes to SD.");
    return;
  }

  if (pending_bytes + packet_size > sizeof(write_buffer)) {
    // Should not happen since we derive buffer size from max packet size.
    Error_Handler();
  }

  // Split the packet into two parts, the number of bytes that will be
  // written now and the number of bytes that will stay pending for
  // a future write.
  const uint16_t packet_bytes_left_over =
      (pending_bytes + packet_size) % _MAX_SS;
  const uint16_t packet_bytes_to_write = packet_size - packet_bytes_left_over;

  // Maybe write pending and packet part 1.
  packet.reset_reading();
  if (packet_bytes_to_write) {
    // Append part 1 to the pending bytes.
    packet.read_bytes(&write_buffer[pending_bytes], packet_bytes_to_write);
    if (packet.had_read_errors()) {
      // Should not happen since we verified the size.
      Error_Handler();
    }
    pending_bytes += packet_bytes_to_write;

    // Write to SD.
    internal_write_pending_bytes();
  }

  // Maybe append left over packet bytes to pending bytes.
  if (packet_bytes_left_over) {
    packet.read_bytes(&write_buffer[pending_bytes], packet_bytes_left_over);
    if (packet.had_read_errors()) {
      // Should not happen since we derived from packet size.
      Error_Handler();
    }
    pending_bytes += packet_bytes_left_over;
  }

  if (!packet.all_read_ok()) {
    // Should not happen since we derived from packet size.
    Error_Handler();
  }

  records_written++;
  // logger.info("Wrote SD record %lu, size=%hu", records_written, packet_size);

  // printf("\n");
  // for (uint32_t i = 0; i < n; i++) {
  //   printf("%02hx ", buffer[i]);
  // }
  // printf("\n\n");

  // if (records_written == kMaxRecordsToWrite) {
  //   logger.info("Closing SD log file");
  //   internal_close_log_file();
  // }
}

bool is_recording_active() {
  MutexScope scope(mutex);
  return (state == STATE_OPENED);
}

void get_current_recording_name(RecordingName* name) {
  name->set_c_str(current_session_name.c_str());
}

void dump_summary() {
  MutexScope scope(mutex);

  if (state == STATE_IDLE) {
    logger.info("Data Recorder is off.");
    return;
  }

  logger.info("Recording [%s] has %lu records.",
              current_session_name.c_str(), records_written);
}

// bool is_session_log_idle() {
//   MutexScope scope(mutex);
//   return state == STATE_IDLE;
// }

}  // namespace data_recorder