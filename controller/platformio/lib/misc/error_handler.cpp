#include "error_handler.h"

#include "main.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

void App_Error_Handler() { App_Error_Handler(); }

// This is also called by HAL and the cube_ide libraries.
void Error_Handler() {
  __disable_irq();
  // TODO: Blink LEDs or something.
  while (1) {
    asm("nop");
  }
}