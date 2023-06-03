#include "serial.h"

namespace serial {
Serial serial1(&huart1);
}  // namespace serial

// Takes over the default weak handler. Called for all serial
// ports.
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart == serial::serial1._huart) {
    serial::serial1.tx_next_chunk();
  } else {
    asm("nop");
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart == serial::serial1._huart) {
    serial::serial1.rx_next_chunk(sizeof(serial::serial1._rx_transfer_buffer));
  } else {
    asm("nop");
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart == serial::serial1._huart) {
    serial::serial1.rx_next_chunk(Size);
  } else {
    asm("nop");
  }
}

//   uint16_t nb_rx_data = huart->RxXferSize - huart->RxXferCount;