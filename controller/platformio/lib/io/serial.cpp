#include "serial.h"

namespace serial {
Serial serial1(&huart1);
}  // namespace serial.

// Serial::Serial(UART_HandleTypeDef *huart) : _huart(huart) {
//   // We registered callback to the handlers. Ufortunally, since the
//   // HAL is implemented in C, we cannot register pointers to instance
//   // method handlers and need to dispatch ourselves to the relevant
//   // serial object.
//   if (!HAL_UART_RegisterCallback(huart, HAL_UART_TX_COMPLETE_CB_ID,
//                                  uart_TxCpltCallback)) {
//     Error_Handler();
//   }
//   if (!HAL_UART_RegisterCallback(huart, HAL_UART_RX_COMPLETE_CB_ID,
//                                  uart_RxCpltCallback)) {
//     Error_Handler();
//   }
//   if (!HAL_UART_RegisterRxEventCallback(huart, uart_RxEventCallback)) {
//     Error_Handler();
//   }
// }

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

//   uint16_t nb_rx_data = huart->RxXferSize - huart->RxXferCount;