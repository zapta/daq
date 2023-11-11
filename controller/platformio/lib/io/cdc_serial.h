#pragma once

#include <inttypes.h>
#include "static_task.h"

namespace cdc_serial {

void setup();
void write_str(const char* str);
void write(const uint8_t* bfr, uint16_t len);

// Caller should provide a task to run this task body.
extern TaskBodyFunction logger_task_body;

}  // namespace cdc_serial
