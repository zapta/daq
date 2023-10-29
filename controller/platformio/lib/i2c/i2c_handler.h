#pragma once

namespace i2c {

// Does not return.
void i2c_task_body(void* argument);

void dump_state();

}  // namespace i2c
