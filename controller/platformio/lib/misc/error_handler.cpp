#include "error_handler.h"

#include "main.h"

#pragma GCC push_options
#pragma GCC optimize("Og")

void App_Error_Handler() { Error_Handler(); }

// This is also called by HAL and the cube_ide libraries.
void Error_Handler() {
  __disable_irq();
  // TODO: Blink LEDs or something.
  while (1) {
    asm("nop");
  }
}

#pragma GCC pop_options
