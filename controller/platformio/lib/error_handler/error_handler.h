#pragma once

#include "main.h"

namespace error_handler {
// Trap a fatal error of given code. e should be in [1, 999]
// with zeros allows only as leading zeros, otherwise force
// to be 1. E.g. 208 is displayed as 218 since we cannot signal
// the digit 0.
// App code should call this function rather than then Error_Handler
// which is used by HAL and cube_ide libraries.
__attribute__((noreturn)) void Panic(uint32_t e);

// Exposed for unit testing only.
void signaling_delay(float t);

}  // namespace error_handler
