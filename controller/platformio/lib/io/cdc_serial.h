#pragma once

#include <inttypes.h>

namespace cdc_serial {

void setup();
void write_str(const char* str);
void write(const uint8_t* bfr, uint16_t len);

void logger_task_body(void* argument);

// void tx_worker_task_body(void* argument);

// Main should provide a task for this body.
// void _tx_task_body(void* argument) {


}  // namespace cdc_serial