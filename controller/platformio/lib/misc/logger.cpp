
#include "logger.h"

#include "FreeRTOS.h"
#include "cdc_serial.h"
#include "io.h"
#include "static_mutex.h"
#include "semphr.h"
#include "string.h"

// The main logger.
Logger logger;

// We use a shared static buffer, protected by a semaphore.
static char tmp_buffer[120];
// static SemaphoreHandle_t semaphore_handle = nullptr;
static StaticMutex mutex;

void Logger::_vlog(const char* level_str, const char* format,
                   va_list args) const {
  // Ad hoc initialization. Assuming no race condition on first call.
  // if (!semaphore_handle) {
  //   semaphore_handle = xSemaphoreCreateBinary();
  //   xSemaphoreGive(semaphore_handle);
  // }

  // io::TEST1.on();

  // xSemaphoreTake(semaphore_handle, portMAX_DELAY);

  // io::TEST1.off();
  {
    MutexScope mutex_scope(mutex);

    strcpy(tmp_buffer, level_str);
    strcat(tmp_buffer, ": ");
    const int prefix_len = strlen(tmp_buffer);
    // We reserve two bytes for \n and null terminator, potentially truncating
    // the message.
    const int msg_len =
        vsnprintf(tmp_buffer + prefix_len, sizeof(tmp_buffer) - prefix_len - 2, format, args);
    strcpy(&tmp_buffer[prefix_len + msg_len], "\n");
    cdc_serial::write_str(tmp_buffer);
  }
  // xSemaphoreGive(semaphore_handle);
}