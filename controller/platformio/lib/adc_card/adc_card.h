// Handler of the load cell and temperature ADC board (ADS1261/SPI).
// Not to be confused with the current/voltage I2C ADC card.

// TODO: Find a better name for this module.

#pragma once

#include "static_task.h"

namespace adc_card {

// Caller should provide a task for this task body.
extern TaskBodyFunction adc_card_task_body;

void dump_state();

// For diagnostics.
void verify_static_registers_values();

}  // namespace adc_card
