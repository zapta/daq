#pragma once

// TODO: Consider to skip the HAL and access gpio registers directly.

#include "main.h"

class OutputPin {
 public:
  OutputPin(GPIO_TypeDef* gpio_port, uint16_t gpio_pin, bool inverted,
            bool initial_value)
      : _gpio_port(gpio_port), _gpio_pin(gpio_pin), _inverted(inverted) {
    set(initial_value);
  }

inline void high() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin, GPIO_PIN_SET);
  }

    inline void low() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin, GPIO_PIN_RESET );
  }

  inline void on() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin,
                      _inverted ? GPIO_PIN_RESET : GPIO_PIN_SET);
  }

  inline void off() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin,
                      _inverted ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }

  inline void set(bool is_on) {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin,
                      (is_on == _inverted) ? GPIO_PIN_RESET : GPIO_PIN_SET);
  }

  inline void toggle() { HAL_GPIO_TogglePin(_gpio_port, _gpio_pin); }

 private:
  GPIO_TypeDef* const _gpio_port;
  const uint16_t _gpio_pin;
  const bool _inverted;
};

class InputPin {
 public:
  InputPin(GPIO_TypeDef* gpio_port, uint16_t gpio_pin)
      : _gpio_port(gpio_port), _gpio_pin(gpio_pin) {}

  inline bool read() { return HAL_GPIO_ReadPin(_gpio_port, _gpio_pin); }

 private:
  GPIO_TypeDef* const _gpio_port;
  const uint16_t _gpio_pin;
};

namespace io {

// Port E3. Not available on PCIE.
extern OutputPin LED;
// Port D1 = PCIE B13.
extern OutputPin TEST1;

// High when pressed.
extern InputPin USER_SWITCH;

// High when SD card is inserted.
// extern InputPin SD_SWITCH;

}  // namespace io.