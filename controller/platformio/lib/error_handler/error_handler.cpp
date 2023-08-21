#include "error_handler.h"

#include "gpio_pins.h"
#include "main.h"

// IMPORTANT: This file should be compiled with optimization off.
// Enabling the optimization will affect the error code signaling
// speed.
#pragma GCC push_options
#pragma GCC optimize("O0")


namespace error_handler {

static constexpr uint64_t kDelayUint = 2500000;


// Busy loop delay, in units of one beat.
 void signaling_delay(float t) {
  // HAL_GetTick();

  const uint64_t l = kDelayUint * t;
  for (uint64_t i = 0; i < l; i++) {
    asm("nop");
  }
}

// Signal one digit
static void send_digit(uint8_t n) {
  if (n < 1) {
    n = 1;
  }
  if (n > 9) {
    n = 9;
  }
  signaling_delay(7);  // pre digit space
  for (int i = 0; i < n; i++) {
    gpio_pins::LED.set_high();
    signaling_delay(0.4);
    gpio_pins::LED.set_low();
    signaling_delay(1.6);
  }
}

 void Panic(uint32_t e) {
  __disable_irq();

  for (;;) {
    // Limit to three digits
    if (e > 999) {
      e = 999;
    }

    uint32_t d1 = e / 100;
    uint32_t d2 = (e % 100) / 10;
    uint32_t d3 = e % 10;

    // Start marker (long pulse)
    gpio_pins::LED.set_low();
    signaling_delay(8.0);  // Pre sycle space
    gpio_pins::LED.set_high();
    signaling_delay(10.0);
    gpio_pins::LED.set_low();

    // Emit the digits
    if (d1 > 0) {
      send_digit(d1);
    }
    if (d1 > 0 || d2 > 0) {
      send_digit(d2);
    }
    send_digit(d3);
  }
}



} // namespace error_handler


// Error_Handler() is declared by the clube_ide file main.h.
// This is also called by HAL and the cube_ide libraries.
void Error_Handler() {
  error_handler::Panic(3);

  // __disable_irq();
  // // TODO: Blink LEDs or something.
  // while (1) {
  //   asm("nop");
  // }
}


#pragma GCC pop_options

