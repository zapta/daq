// Handler of the load cell and temperature ADC board (ADS1261/SPI).
// Not to be confused with the current/voltage I2C ADC card.

// TODO: Find a better name for this module.

#pragma once

namespace adc {

// Does not return.
void adc_task_body(void* argument);

void dump_state();

// For diagnostics.
void verify_registers_vals();

}  // namespace adc
