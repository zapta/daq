
#include "tasks.h"
#include "cdc_serial.h"
#include "host_link.h"

namespace tasks {

StaticTask<1000> cdc_logger_task(cdc_serial::logger_task_body, "Logger", 10);

StaticTask<1000> main_task(main_task_body, "Main", 10);

StaticTask<1000> host_link_rx_task(host_link::rx_task_body, "Host RX", 10);

}  // namespace tasks