#include "serial.h"

namespace serial {
Serial serial1(&huart1);
Serial serial2(&huart2);

// Finds the serial by huart. Fatal error if not found.
Serial *get_serial_by_huart(UART_HandleTypeDef *huart) {
  if (huart == &huart1) {
    return &serial::serial1;
  }
  if (huart == &huart2) {
    return &serial::serial2;
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
  BaseType_t task_woken = pdFALSE;
  serial->rx_next_chunk_isr(sizeof(serial->_rx_transfer_buffer), &task_woken);
  portYIELD_FROM_ISR(task_woken)
}

// Called in case of reciever timeout, with partial buffer size.
void Serial::uart_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size) {
  Serial *serial = serial::get_serial_by_huart(huart);
  BaseType_t task_woken = pdFALSE;
  serial->rx_next_chunk_isr(size, &task_woken);
  portYIELD_FROM_ISR(task_woken)
}
