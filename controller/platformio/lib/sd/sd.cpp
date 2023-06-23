#include "sd.h"

#include <cstring>

#include "fatfs.h"
#include "logger.h"
#include "sdmmc.h"
#include "static_mutex.h"

// extern SD_HandleTypeDef hsd1;

namespace sd {

// All vars are protected by the mutex.
static uint8_t rtext[_MAX_SS];
static int init_level = 0;
static uint8_t buffer[serial_packets_consts::MAX_STUFFED_PACKET_LEN];

StaticMutex mutex;

bool open_log_file(const char* name) {
  MutexScope scope(mutex);

  if (init_level != 0) {
    logger.error("Log file open, init_level=%d", init_level);
    return false;
  }

  FRESULT status = f_mount(&SDFatFS, (TCHAR const*)SDPath, 0);
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_mount failed, status=%d", status);
    return false;
  }
  init_level = 1;

  status = f_mkfs((TCHAR const*)SDPath, FM_ANY, 0, rtext, sizeof(rtext));
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_mkfs failed, status=%d", status);
    return false;
  }
  init_level = 2;

  status = f_open(&SDFile, name, FA_CREATE_ALWAYS | FA_WRITE);
  if (status != FRESULT::FR_OK) {
    logger.error("SD f_open failed, status=%d", status);
    return false;
  }
  init_level = 3;

  return true;
}

void close_log_file() {
  MutexScope scope(mutex);

  if (init_level == 3) {
    f_close(&SDFile);
    init_level = 2;
  }

  if (init_level > 0) {
    f_mount(&SDFatFS, (TCHAR const*)NULL, 0);
    init_level = 0;
  }
}

bool append_to_log_file(const StuffedPacketBuffer& packet) {
  MutexScope scope(mutex);

  // Check state.
  if (init_level != 3) {
    logger.error("Can't write log file, status=%d", init_level);
    return false;
  }

  // Determine packet size.
  const uint16_t n = packet.size();
  if (!n) {
    return true;
  }
  if (n > sizeof(buffer)) {
    // Should not happen since we derive buffer size from max packet size.
    Error_Handler();
  }

  // Extract packet bytes.
  packet.reset_reading();
  packet.read_bytes(buffer, n);
  if (!packet.all_read_ok()) {
    // Should not happen since we verified the size.
    Error_Handler();
  }

  // Write to SD.
  unsigned int bytes_written;
  FRESULT status = f_write(&SDFile, buffer, n, &bytes_written);
  if (status != FRESULT::FR_OK) {
    logger.error("Error writing to SD log file, status=%d", status);
    return false;
  }
  if (bytes_written != n) {
    logger.error("Requested to write to SD %hu bytes, %u written", n,
                 bytes_written);
    return false;
  }
  status = f_sync(&SDFile);
  if (status != FRESULT::FR_OK) {
    logger.warning("Failed to flush SD file, status=%d", status);
    return false;
  }

  // All done ok.
  return true;
}

bool is_log_file_open_ok() {
  MutexScope scope(mutex);
  return init_level == 3;
}

//-----

// void test_setup() {
//   FRESULT res;           /* FatFs function common result code */
//   uint32_t byteswritten; /* File write counts */
//   uint8_t wtext[] = "STM32 FATFS works great!"; /* File write buffer */
//   uint8_t rtext[_MAX_SS];                       /* File read buffer */

//   if (f_mount(&SDFatFS, (TCHAR const*)SDPath, 0) != FR_OK) {
//     Error_Handler();
//   } else {
//     if (f_mkfs((TCHAR const*)SDPath, FM_ANY, 0, rtext, sizeof(rtext)) !=
//         FR_OK) {
//       Error_Handler();
//     } else {
//       // Open file for writing (Create)
//       if (f_open(&SDFile, "STM32.TXT", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
//         Error_Handler();
//       } else {
//         // Write to the text file
//         //  res = f_write(&SDFile, wtext, strlen((char *)wtext), (void
//         //  *)&byteswritten);
//         res =
//             f_write(&SDFile, wtext, strlen((char*)wtext), (UINT*)&byteswritten);
//         if ((byteswritten == 0) || (res != FR_OK)) {
//           Error_Handler();
//         } else {
//           f_close(&SDFile);
//         }
//       }
//     }
//   }
//   f_mount(&SDFatFS, (TCHAR const*)NULL, 0);
//   /* USER CODE END 2 */
// }
// void test_loop() {}

}  // namespace sd