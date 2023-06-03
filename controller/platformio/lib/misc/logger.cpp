
#include "logger.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "cdc_serial.h"
#include "string.h"
#include "io.h"

// The main logger.
Logger logger;

// We use a shared static buffer, protected by a semaphore.
static char bfr[120];
static SemaphoreHandle_t semaphore_handle = nullptr;

void Logger::_vlog(const char* level_str, const char* format,
                   va_list args) const {
  // Ad hoc initialization. Assuming no race condition on first call.
  if (!semaphore_handle) {
    semaphore_handle = xSemaphoreCreateBinary();
    xSemaphoreGive(semaphore_handle);
  }

  io::TEST1.on();
  xSemaphoreTake(semaphore_handle, portMAX_DELAY);
  io::TEST1.off();
  {
    strcpy(bfr, level_str);
    strcat(bfr, ": ");
    const int prefix_len = strlen(bfr);
    // We reserve two bytes for \n and null terminator, potentially truncating the message.
    const int msg_len = vsnprintf(bfr + prefix_len, sizeof(bfr) - prefix_len - 2, format, args);
    strcpy(&bfr[prefix_len + msg_len], "\n");
    cdc_serial::write_str(bfr);
  }
  xSemaphoreGive(semaphore_handle);
}