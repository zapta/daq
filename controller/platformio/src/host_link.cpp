#include "host_link.h"
#include "serial_packets_client.h"

namespace host_link {

 SerialPacketsClient client;

void command_handler(uint8_t endpoint, const SerialPacketsData& command_data,
                     uint8_t& response_status,
                     SerialPacketsData& response_data) {
  logger.info("Recieved a command at endpoint %02hhx", endpoint);
  response_data.write_uint16(0x2233);
  response_status = 3;
}

// A callback type for incoming messages.
void message_handler(uint8_t endpoint, const SerialPacketsData& message_data) {
  logger.info("Recieved a message at endpoint %02hhx", endpoint);
}

void setup(Serial& serial) {
    client.begin(serial, command_handler,
                              message_handler);
}

void rx_task_body(void* argument) {
  // This method doesn't return.
  client.rx_task_body();
  Error_Handler();
}


} // namespace host_link