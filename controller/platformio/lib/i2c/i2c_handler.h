#pragma once

namespace i2c_handler {

// Does not return.

void setup();

void i2c_task_body(void* argument);

void i2c_timer_body();


// void dump_state();

}  // namespace i2c_handler
