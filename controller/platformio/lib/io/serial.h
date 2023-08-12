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

#pragma GCC push_options
#pragma GCC optimize("Og")

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

    if (HAL_OK !=
        HAL_UART_RegisterRxEventCallback(_huart, uart_RxEventCallback)) {
      App_Error_Handler();
    }

    // Start the continious RX DMA.
    if (!start_rx_dma()) {
      App_Error_Handler();
    }
  }

  // Celled from a task during initialization or from an ISR in case
  // of an RX error that requires restart. Return true iff OK.
  bool start_rx_dma() {
    // Start the continious circual RX DMA. We pass in the two halves.
    // The UART RX is already specified as cirtucal in the cube_ide settings.
    _rx_last_pos = 0;
    const auto status = HAL_UARTEx_ReceiveToIdle_DMA(_huart, _rx_dma_buffer,
                                                     sizeof(_rx_dma_buffer));
    return status == HAL_OK;
    // App_Error_Handler();
  }

 private:
  // For interrupt handling.
  static void uart_ErrorCallback(UART_HandleTypeDef* huart);
  static void uart_TxCpltCallback(UART_HandleTypeDef* huart);
  // Called whenever new rx data is availble (idle, half and full complete).
  static void uart_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size);

  UART_HandleTypeDef* _huart;
  // --- TX. Non Circular DMA.
  CircularBuffer<uint8_t, 5000> _tx_buffer;
  StaticMutex _tx_mutex;
  // This DMA buffer is non circual.
  uint8_t _tx_dma_buffer[64];

  // ---RX. Circular DMA.
  CircularBuffer<uint8_t, 5000> _rx_buffer;
  StaticMutex _rx_mutex;
  // Indicates that RX buffer has data. Allows to
  // avoid polling of the buffer.
  StaticBinarySemaphore _rx_data_avail_sem;
  // This DMA buffer is circular bytes are added by the DMA
  // in a contingious circular fashion with wrap around.
  uint8_t _rx_dma_buffer[256];
  // One past the last position in _rx_dma_buffer where we
  // consumed data. Modulu the buffer size.
  uint16_t _rx_last_pos = 0;

  // Called in within mutex or from in interrupt. No need to protect access.
  // The caller already verified that tx DMA is not in progress.
  void tx_next_chunk() {
    // At most sizeof(_tx_dma_buffer)
    const uint16_t len =
        _tx_buffer.read(_tx_dma_buffer, sizeof(_tx_dma_buffer));
    if (len > 0) {
      HAL_UART_Transmit_DMA(_huart, _tx_dma_buffer, len);
    }
  }

  // Called from isr to accept new incoming data from the RX DMA buffer.
  void rx_data_arrived_isr(const uint8_t* buffer, uint16_t len,
                           BaseType_t* task_woken) {
    if (len) {
      const bool ok = _rx_buffer.write(buffer, len, true);
      if (!ok) {
        App_Error_Handler();
      }
      // Indicate to the rx thread(s) that data is available.
      _rx_data_avail_sem.give_from_isr(task_woken);
    }
  }

  // _huart->error_code indicates the error code.
  // Search UART_Error_Definition for codes. Errors can
  // be soft, such as RX framing errors, so for now we
  // ignore them.
  void uart_error_isr() {
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
    App_Error_Handler();
  }
};

namespace serial {
// Used by data link.
extern Serial serial1;
// Used by printer link.
extern Serial serial2;
}  // namespace serial

#pragma GCC pop_options
