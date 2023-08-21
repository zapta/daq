#pragma once

#include "main.h"


// Trap a fatal error of given code. e should be in [1, 999]
// with zeros allows only as leading zeros, otherwise force
// to be 1. E.g. 208 is displayed as 218 since we cannot signal
// the digit 0.
// App code should call this function rather than then Error_Handler
// which is used by HAL and cube_ide libraries.
void App_Error_Handler(uint32_t e = 5);

// Temporary for developement.
// void signal_error_code(uint32_t e);
