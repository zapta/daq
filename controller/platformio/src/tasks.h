#pragma once

#include "static_task.h"

void main_task_body(void* argument);

namespace tasks {

extern StaticTask<1000> main_task;
extern StaticTask<1000> cdc_logger_task;
extern StaticTask<1000> host_link_rx_task;

}  // namespace tasks