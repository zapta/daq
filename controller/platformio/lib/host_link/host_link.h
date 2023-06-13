#pragma once

#include "serial.h"
#include "serial_packets_client.h"


namespace host_link {

extern SerialPacketsClient client;

void setup(Serial& serial);

void rx_task_body(void* argument);

}  // namespace host_link