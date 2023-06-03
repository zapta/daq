#pragma once

#include <inttypes.h>

namespace cdc_serial {

void setup();
void write_str(const char* str);
void write(const uint8_t* bfr, uint16_t len);

}  // namespace cdc_serial
