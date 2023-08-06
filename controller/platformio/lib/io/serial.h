// Serial driver. Interrupt driven, no worker tasks.
#pragma once

#include "FreeRTOS.h"
#include "circular_buffer.h"
#include "common.h"
#include "semphr.h"
#include "static_binary_semaphore.h"
#include "static_mutex.h"
#include "time_util.h"
#include "usart.h"

class Serial {
 public:
  Serial(UART_HandleTypeDef* huart) : _huart(huart) {}

  void write_str(const char* str) { write((uint8_t*)str, strlen(str)); }

  void write(uint8_t* bfr, uint16_t len) {
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

  // How many rx bytes are available for consumption.
  uint16_t available() {
    uint16_t result = 0;
    {
      MutexScope mutex_scope(_rx_mutex);
      __disable_irq();
      { result = _rx_buffer.size(); }
      __enable_irq();
    }
    return result;
  }

  // Clear rx/tx buffers. Useful for unit test setup. Note that
  // this doesn't clear in flight HAL rx/tx buffers.
  void clear() {
    MutexScope mutex_scope(_rx_mutex);
    __disable_irq();
    {
      _tx_buffer.clear();
      _rx_buffer.clear();
    }
    __enable_irq();
  }

  // Read without timeout. Returns the number of bytes read into
  // bfr. Gurantees at least one byte but tries maximize the number of
  // bytes returns without adding waiting time.
  uint16_t read(uint8_t* bfr, uint16_t bfr_size) {
    for (;;) {
      // Wait for an indication that data may be available.
      const bool ok = _rx_data_avail_sem.take(portMAX_DELAY);
      if (!ok) {
        // We don't expect a timeout since we block forever.
        App_Error_Handler();
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

  void init() {
    // Register callback handlers.
    if (HAL_OK != HAL_UART_RegisterCallback(_huart, HAL_UART_ERROR_CB_ID,
                                            uart_ErrorCallback)) {
      App_Error_Handler();
    }
    if (HAL_OK != HAL_UART_RegisterCallback(_huart, HAL_UART_TX_COMPLETE_CB_ID,
                                            uart_TxCpltCallback)) {
      App_Error_Handler();
    }
    if (HAL_OK != HAL_UART_RegisterCallback(_huart, HAL_UART_RX_COMPLETE_CB_ID,
                                            uart_RxCpltCallback)) {
      App_Error_Handler();
    }
    if (HAL_OK !=
        HAL_UART_RegisterRxEventCallback(_huart, uart_RxEventCallback)) {
      App_Error_Handler();
    }
    // Start the reception.
    start_hal_rx(true);
  }

 private:
  // For interrupt handling.
  static void uart_ErrorCallback(UART_HandleTypeDef* huart);
  static void uart_TxCpltCallback(UART_HandleTypeDef* huart);
  static void uart_RxCpltCallback(UART_HandleTypeDef* huart);
  static void uart_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size);

  UART_HandleTypeDef* _huart;
  // --- TX
  CircularBuffer<uint8_t, 5000> _tx_buffer;
  StaticMutex _tx_mutex;
  uint8_t _tx_hal_buffer[20];

  // ---RX
  CircularBuffer<uint8_t, 5000> _rx_buffer;
  StaticMutex _rx_mutex;
  // Indicates that RX buffer has data. Allows to
  // avoid polling of the buffer.
  StaticBinarySemaphore _rx_data_avail_sem;
  uint8_t _rx_hal_buffer[20];

  // Called in within mutex or from in interrupt. No need to protect access.
  void tx_next_chunk() {
    const uint16_t len =
        _tx_buffer.read(_tx_hal_buffer, sizeof(_tx_hal_buffer));
    if (len > 0) {
      HAL_UART_Transmit_IT(_huart, _tx_hal_buffer, len);
    }
  }

  // Called from isr to accept new incoming data.
  void rx_next_chunk_isr(uint16_t len, BaseType_t* task_woken) {
    if (len) {
      const bool ok = _rx_buffer.write(_rx_hal_buffer, len, true);
      if (!ok) {
        App_Error_Handler();
      }
      // Indicate that data is available.
      _rx_data_avail_sem.give_from_isr(task_woken);
    }
    start_hal_rx(false);
  }

  // _huart->error_code indicates the error code.
  // Search UART_Error_Definition for codes.
  void uart_error_isr() {
    // TODO: Count errors by type. Some of the errors
    // are soft.
  }

// Called from a task (init) or from RX isr.
  void start_hal_rx(bool initializing) {
    for (int i = 1;; i++) {
      const HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_IT(
          _huart, _rx_hal_buffer, sizeof(_rx_hal_buffer));
      if (status == HAL_OK) {
        return;
      }
      // During initialization we try more than once, as a 
      // workaround for a HAL problem with left over serial data (?).
      // Especially noticeable when running in the debugger with 
      // monitor program active.
      if (initializing && i < 3) {
        continue;
      }
      App_Error_Handler();
    }
  }
};

namespace serial {
// Used by data link.
extern Serial serial1;
// Used by printer link.
extern Serial serial2;
}  // namespace serial