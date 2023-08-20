
#include "logger.h"

#include "FreeRTOS.h"
#include "cdc_serial.h"
// #include "gpio_pins.h"
#include "static_mutex.h"
#include "semphr.h"
#include "string.h"

// The main logger.
Logger logger;

// We use a shared static buffer, protected by a semaphore.
static char line_buffer[200];
static StaticMutex mutex;

void Logger::_vlog(const char* level_str, const char* format,
                   va_list args) const {

  {
    MutexScope mutex_scope(mutex);

    strcpy(line_buffer, level_str);
    strcat(line_buffer, ": ");
    const int prefix_len = strlen(line_buffer);
    // We reserve two bytes for \n and null terminator, potentially truncating
    // the message.
    const int msg_len =
        vsnprintf(line_buffer + prefix_len, sizeof(line_buffer) - prefix_len - 2, format, args);
    strcpy(&line_buffer[prefix_len + msg_len], "\n");
    cdc_serial::write_str(line_buffer);
  }
}