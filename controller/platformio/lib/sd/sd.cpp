#include "sd.h"

#include <cstring>

#include "cdc_serial.h"
#include "fatfs.h"
#include "logger.h"
#include "sdmmc.h"
#include "static_mutex.h"

// extern SD_HandleTypeDef hsd1;

namespace sd {

// All vars are protected by the mutex.
// static uint8_t rtext[_MAX_SS*8];

static int init_level = 0;

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

// static constexpr uint32_t kMaxRecordsToWrite = 1000;

StaticMutex mutex;

// Grab mutex before calling this one.
// Tries to write pending bytes to SD. Always
// clear number of pending bytes.
static void internal_write_pending_bytes() {
  const uint32_t n = pending_bytes;
  pending_bytes = 0;
  unsigned int bytes_written;
  // This number should be a multipe of _MAX_SS.
  logger.info("Writing to SD %lu bytes", n);
  FRESULT status = f_write(&SDFile, write_buffer, n, &bytes_written);
  if (status != FRESULT::FR_OK) {
    logger.error("Error writing to SD log file, status=%d", status);
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
static void internal_close_log_file() {
  if (init_level == 2) {
    internal_write_pending_bytes();
    f_close(&SDFile);
    init_level--;
  }

  if (init_level == 1) {
    f_mount(&SDFatFS, (TCHAR const*)NULL, 0);
    init_level--;
  }
}

bool open_log_file(const char* name) {
  MutexScope scope(mutex);

  if (init_level != 0) {
    logger.error("Log file open, init_level=%d", init_level);
    return false;
  }

  pending_bytes = 0;

  FRESULT status = f_mount(&SDFatFS, (TCHAR const*)SDPath, 0);
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_mount failed, status=%d", status);
    return false;
  }
  init_level = 1;

   status = f_open(&SDFile, name, FA_CREATE_ALWAYS | FA_WRITE);
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_open failed, status=%d", status);
    return false;
  }
  init_level = 2;

  return true;
}

void close_log_file() {
  MutexScope scope(mutex);

  internal_close_log_file();
}

void append_to_log_file(const StuffedPacketBuffer& packet) {
  MutexScope scope(mutex);

  if (packet.had_write_errors()) {
    logger.error("SD a log packet has write errors");
    return;
  }

  // if (records_written >= kMaxRecordsToWrite) {
  //   logger.info("Aleady writen %lu records", kMaxRecordsToWrite);
  //   return;
  // }

  // Check state.
  if (init_level != 2) {
    logger.error("Can't write log file, status=%d", init_level);
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
  logger.info("Wrote SD record %lu, size=%hu", records_written,
   packet_size);

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

bool is_log_file_open_ok() {
  MutexScope scope(mutex);
  return init_level == 2;
}

}  // namespace sd