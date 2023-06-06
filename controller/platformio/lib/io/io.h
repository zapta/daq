#pragma once

// TODO: Consider to skip the HAL and access gpio registers directly.

#include "main.h"

class OutputPin {
 public:
  OutputPin(GPIO_TypeDef* gpio_port, uint16_t gpio_pin, bool inverted,
            bool initial_value)
      : _gpio_port(gpio_port),
        _gpio_pin(gpio_pin),
        _inverted(inverted)
        // _initial_value(initial_value)
         {
    set(initial_value);
  }



  inline void on() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin,
                      _inverted ?  GPIO_PIN_RESET : GPIO_PIN_SET );
  }

  inline void off() {
    HAL_GPIO_WritePin(_gpio_port, _gpio_pin,
                      _inverted ? GPIO_PIN_SET:  GPIO_PIN_RESET);
  }

  inline void set(bool is_on) {
    HAL_GPIO_WritePin(
        _gpio_port, _gpio_pin,
        (is_on == _inverted) ? GPIO_PIN_RESET :  GPIO_PIN_SET );
  }

  inline void toggle() { HAL_GPIO_TogglePin(_gpio_port, _gpio_pin); }

 private:
  GPIO_TypeDef* const _gpio_port;
  const uint16_t _gpio_pin;
  const bool _inverted;
};

class InputPin {
 public:
  InputPin(GPIO_TypeDef* gpio_port, uint16_t gpio_pin) : _gpio_port(gpio_port), _gpio_pin(gpio_pin) {
    // gpio_set_direction(pin_num_, GPIO_MODE_INPUT);
    // gpio_set_pull_mode(pin_num_, pull_mode);
  }
  inline bool read() { 
       HAL_GPIO_ReadPin(_gpio_port, _gpio_pin);
        // _gpio_port, _gpio_pin,
        // (is_on == _inverted) ? GPIO_PIN_RESET :  GPIO_PIN_SET );
   }

  // inline bool is_high() { return read(); }
  // inline bool is_low() { return !read(); }

  // inline gpio_num_t pin_num() { return pin_num_; }

 private:
   GPIO_TypeDef* const _gpio_port;
  const uint16_t _gpio_pin;
  // const gpio_num_t pin_num_;
};

namespace io {

void setup();

extern OutputPin LED;
extern OutputPin TEST1;
extern OutputPin TEST2;
extern OutputPin TEST3;

extern InputPin SWITCH;


}  // namespace io.