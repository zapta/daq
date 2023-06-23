#include "serial.h"

namespace serial {
Serial serial1(&huart1);
}  // namespace serial.



void Serial::uart_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart == serial::serial1._huart) {
    serial::serial1.tx_next_chunk();
  } else {
    asm("nop");
  }
}

void Serial::uart_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart == serial::serial1._huart) {
    serial::serial1.rx_next_chunk(sizeof(serial::serial1._rx_transfer_buffer));
  } else {
    asm("nop");
  }
}

// Called in case of idle gap, with partial buffer size.
void Serial::uart_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart == serial::serial1._huart) {
    serial::serial1.rx_next_chunk(Size);
  } else {
    asm("nop");
  }
}

