#include "cdc_serial.h"

#include <algorithm>
#include <cstring>

#include "FreeRTOS.h"
#include "circular_buffer.h"
#include "main.h"
#include "semphr.h"
#include "static_mutex.h"
#include "static_task.h"
#include "task.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "time_util.h"

// An helper for printf(). Do not printf() before calling setup() here.
// TODO: Check the status returned from the semaphore functions.
extern "C" {
extern int _write(int, uint8_t*, int);
int _write(int file, uint8_t* ptr, int len) {
  cdc_serial::write(ptr, len);
  return len;
}
}

namespace cdc_serial {

// A circual buffer of pending bytes.
// static constexpr uint16_t kBufferSize = 1000;
// static uint8_t buffer[kBufferSize];
static CircularBuffer<uint8_t, 5000> circular_buffer;

// Semaphore to protect access to the buffer.
// static SemaphoreHandle_t semaphore_handle = nullptr;
static StaticMutex mutex;

// Temp buffer for sending data.
static uint8_t tx_buffer[100];

static void logger_task_body(void* ignored_argument) {
  for (;;) {
    // Transfer a chunk of data to tx_buffer, if avaiable.
    uint16_t bytes_to_send = 0;
    // xSemaphoreTake(semaphore_handle, portMAX_DELAY);
    {
      MutexScope mutex_scope(mutex);
      bytes_to_send = circular_buffer.read(tx_buffer, sizeof(tx_buffer));
    }
    // xSemaphoreGive(semaphore_handle);

    if (bytes_to_send) {
      for (;;) {
        const uint8_t rc = CDC_Transmit_FS(tx_buffer, bytes_to_send);
        // Exit on error or ok. 
        // NOTE: We ignored silentcy the errors USBD_EMEM and USBD_FAIL.
        if (rc != USBD_BUSY) {
          break;
        }
        time_util::delay_millis(1);
      }
    } else {
      // Nothing to transmit. Wait and try again.
      time_util::delay_millis(50);
    }
  }
}

void write_str(const char* str) {
  const uint16_t len = strlen(str);
  write((uint8_t*)str, len);
}

void write(const uint8_t* bfr, uint16_t len) {
  {
    MutexScope mutex_scope(mutex);
    circular_buffer.write(bfr, len, true);
  }
}

// The exported runnable
StaticRunnable logger_task_runnable(logger_task_body, nullptr);

}  // namespace cdc_serial
