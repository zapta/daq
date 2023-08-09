#include "serial.h"

#include "common.h"

// #pragma GCC optimize("Og")

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
    App_Error_Handler();
  }
}

}  // namespace serial.

void Serial::uart_TxCpltCallback(UART_HandleTypeDef *huart) {
  Serial *serial = serial::get_serial_by_huart(huart);
  serial->tx_next_chunk();
}

void Serial::uart_ErrorCallback(UART_HandleTypeDef *huart) {
  asm("nop");
  Serial *serial = serial::get_serial_by_huart(huart);
  serial->uart_error_isr();
}

// Called in case of reciever timeout, with partial buffer size.
// Based on an example at
// https://community.st.com/t5/wireless-mcu/hal-uartex-receivetoidle-dma-idle-event-what-is-that-whatever-it/m-p/141617/highlight/true#M5301
void Serial::uart_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size) {
  // Map the HAL's hurart to our Serial wrapper class.
  Serial *serial = serial::get_serial_by_huart(huart);

  const uint16_t new_pos = size;
  constexpr size_t kBufferSize = sizeof(serial->_rx_dma_buffer);

  // Assertion: Wrap around cannot happen within one invocation
  // since the handler is invoked also by full complete.
  if (new_pos < serial->_rx_last_pos) {
    Error_Handler();
  }

  // Assertion: New pos can be equal to the buffer size, e.g.
  // on a full complete event, but not above it.
  if (new_pos > kBufferSize) {
    Error_Handler();
  }

  // Copy the new bytes.
  BaseType_t task_woken = pdFALSE;
  const uint32_t len = new_pos - serial->_rx_last_pos;
  serial->rx_data_arrived_isr(&serial->_rx_dma_buffer[serial->_rx_last_pos],
                              len, &task_woken);

  // Remember the new position, adjusting to for wrap around.
  serial->_rx_last_pos =
      new_pos >= kBufferSize ? (new_pos - kBufferSize) : new_pos;

  portYIELD_FROM_ISR(task_woken)
}
