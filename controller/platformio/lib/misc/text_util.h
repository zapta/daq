#pragma once

// TODO: Add unit tests

#include "stdint.h"
#include "string.h"

namespace text_util {

// bfr_size is buffer size in uint16_t units. Return true if ok.
bool wstr_from_str(uint16_t* bfr, int bfr_size, const char* str) {
  if (bfr_size < 1) {
    // Can't even set a null terminator.
    return false;
  }
  const int n = strlen(str) + 1;
  if (n > bfr_size) {
    bfr[0] = 0;
    return false;
  }
  // Copy the str and null terminator.
  for (int i = 0; i < n; i++) {
    bfr[i] = (uint8_t)str[i];
  }
  return true;
}

}  // namespace text_util