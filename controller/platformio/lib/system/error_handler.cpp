#include "main.h"

void Error_Handler(void) {
  __disable_irq();
  // TODO: Blink LEDs or something.
  while (1) {
  }
}