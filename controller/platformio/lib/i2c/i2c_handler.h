#pragma once

namespace i2c {

// Does not return.

void i2c_setup();

void i2c_task_body(void* argument);

void i2c_timer_body();


// void dump_state();

}  // namespace i2c
