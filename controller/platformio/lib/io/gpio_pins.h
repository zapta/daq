#pragma once

// TODO: Consider to skip the HAL and access gpio registers directly.

// NOTE: We do not call this file 'gpio.h' to avoid a conflict with the
// cube_ide file of same name.

#include "main.h"

// An abstraction class for pins that were defined in cube_ide
// as gpio output pins.
class OutputPin {
 public:
  OutputPin(GPIO_TypeDef* gpio_port, uint16_t gpio_pin, bool initial_value)
      : _gpio_port(gpio_port), _gpio_pin(gpio_pin) {
    set(initial_value);
  }

  inline void set_high() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin, GPIO_PIN_SET);
  }

  inline void set_low() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin, GPIO_PIN_RESET);
  }

  inline void set(bool is_high) {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin,
                      is_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }

  inline void toggle() { HAL_GPIO_TogglePin(_gpio_port, _gpio_pin); }

 private:
  GPIO_TypeDef* const _gpio_port;
  const uint16_t _gpio_pin;
  // const bool _inverted;
};

// An abstraction class for pins that were defined in cube_ide
// as gpio input pins.
class InputPin {
 public:
  InputPin(GPIO_TypeDef* gpio_port, uint16_t gpio_pin)
      : _gpio_port(gpio_port), _gpio_pin(gpio_pin) {}

  // inline bool read() { return HAL_GPIO_ReadPin(_gpio_port, _gpio_pin); }

  inline bool is_high() { return HAL_GPIO_ReadPin(_gpio_port, _gpio_pin); }
  inline bool is_low() { return !is_high(); }

 private:
  GPIO_TypeDef* const _gpio_port;
  const uint16_t _gpio_pin;
};

namespace gpio_pins {

// Port E3. Not available on PCIE.
extern OutputPin LED;
// Port D1 = PCIE B13.
extern OutputPin TEST1;

// High when pressed.
extern InputPin USER_SWITCH;

// High when the SD card is inserted. This input also read
// independently by the FatFS library.
extern InputPin SD_SWITCH;

}  // namespace gpio_pins.