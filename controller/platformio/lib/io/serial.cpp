#include "serial.h"

namespace serial {
Serial serial1(&huart1);

// Finds the serial by huart. Fatal error if not found.
Serial *get_serial_by_huart(UART_HandleTypeDef *huart) {
  if (huart == &huart1) {
    return &serial::serial1;
  }
  // Not found.
  for (;;) {
    Error_Handler();
  }
}

}  // namespace serial.

void Serial::uart_TxCpltCallback(UART_HandleTypeDef *huart) {
  Serial *serial = serial::get_serial_by_huart(huart);
  serial->tx_next_chunk();
}

void Serial::uart_RxCpltCallback(UART_HandleTypeDef *huart) {
  Serial *serial = serial::get_serial_by_huart(huart);
  serial->rx_next_chunk(sizeof(serial->_rx_transfer_buffer));
}

// Called in case of reciever timeout, with partial buffer size.
void Serial::uart_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size) {
  Serial *serial = serial::get_serial_by_huart(huart);
  serial->rx_next_chunk(size);
}
