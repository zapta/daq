#include "serial.h"

#include "common.h"

// #pragma GCC push_options
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
    error_handler::Panic(84);
  }
}

}  // namespace serial.

void Serial::tx_next_chunk() {
  // At most sizeof(_tx_dma_buffer)
  const uint16_t len = _tx_buffer.read(_tx_dma_buffer, sizeof(_tx_dma_buffer));
  if (len > 0) {
    HAL_UART_Transmit_DMA(_huart, _tx_dma_buffer, len);
  }
}

// Called from isr to accept new incoming data from the RX DMA buffer.
void Serial::rx_data_arrived_isr(const uint8_t *buffer, uint16_t len,
                                 BaseType_t *task_woken) {
  if (len) {
    const bool ok = _rx_buffer.write(buffer, len, true);
    if (!ok) {
      error_handler::Panic(67);
    }
    // Indicate to the rx thread(s) that data is available.
    _rx_data_avail_sem.give_from_isr(task_woken);
  }
}

void Serial::uart_error_isr() {
  // TODO: Count errors by type. Some of the errors
  // are soft.

  // Do nothing if RX still active.
  if (_huart->RxState == HAL_UART_STATE_BUSY_RX) {
    return;
  }

  // If idle, try to restart.
  if (_huart->RxState == HAL_UART_STATE_READY) {
    for (int i = 0; i < 10; i++) {
      if (start_rx_dma()) {
        return;
      }
    }
  }

  // TODO: Anything we should do to recover? E.g. abort and then restart?
  error_handler::Panic(68);
}

void Serial::write(uint8_t *bfr, uint16_t len) {
  for (;;) {
    bool written = false;
    bool tx_in_progress = false;
    {
      MutexScope mutex_scope(_tx_mutex);
      __disable_irq();
      {
        written = _tx_buffer.write(bfr, len);
        tx_in_progress = _huart->gState & 0x01;
      }
      __enable_irq();
      if (!tx_in_progress) {
        tx_next_chunk();
      }
    }
    if (written) {
      return;
    }
    // Wait and try again.
    time_util::delay_millis(5);
  }
}

uint16_t Serial::read(uint8_t *bfr, uint16_t bfr_size) {
  for (;;) {
    // Wait for an indication that data may be available.
    const bool ok = _rx_data_avail_sem.take(portMAX_DELAY);
    if (!ok) {
      // We don't expect a timeout since we block forever.
      error_handler::Panic(61);
    }

    // Try to read the  data from the rx buffer.
    int bytes_read = 0;
    bool bytes_left = false;
    {
      MutexScope mutex_scope(_rx_mutex);
      __disable_irq();
      {
        bytes_read = _rx_buffer.read(bfr, bfr_size);
        bytes_left = !_rx_buffer.is_empty();
      }
      __enable_irq();
    }
    // If there is data left in the rx buffer, preserve the data
    // available status.
    if (bytes_left) {
      _rx_data_avail_sem.give();
    }
    if (bytes_read) {
      return bytes_read;
    }

    // Theoretically we can reach here if two tasks take
    // from the data avail semaphore but one consumes all
    // the rx data.
  }
}

void Serial::init() {
  // Register callback handlers.
  if (HAL_OK != HAL_UART_RegisterCallback(_huart, HAL_UART_ERROR_CB_ID,
                                          uart_ErrorCallback)) {
    error_handler::Panic(62);
  }
  if (HAL_OK != HAL_UART_RegisterCallback(_huart, HAL_UART_TX_COMPLETE_CB_ID,
                                          uart_TxCpltCallback)) {
    error_handler::Panic(63);
  }

  if (HAL_OK !=
      HAL_UART_RegisterRxEventCallback(_huart, uart_RxEventCallback)) {
    error_handler::Panic(64);
  }

  // Start the continious RX DMA.
  if (!start_rx_dma()) {
    error_handler::Panic(65);
  }
}

bool Serial::start_rx_dma() {
  // Start the continious circual RX DMA. We pass in the two halves.
  // The UART RX is already specified as cirtucal in the cube_ide settings.
  _rx_last_pos = 0;
  const auto status = HAL_UARTEx_ReceiveToIdle_DMA(_huart, _rx_dma_buffer,
                                                   sizeof(_rx_dma_buffer));
  return status == HAL_OK;
  // error_handler::Panic(66);
}

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
  // NOTE: May happen if breaking with a debugger. Try to disconnect the serial
  // cable.
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
