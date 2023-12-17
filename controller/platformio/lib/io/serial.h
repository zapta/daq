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

// #pragma GCC push_options
// #pragma GCC optimize("Og")

class Serial {
 public:
  Serial(UART_HandleTypeDef* huart) : _huart(huart) {}

  void write_str(const char* str) { write((uint8_t*)str, strlen(str)); }

  void write(uint8_t* bfr, uint16_t len);

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
  uint16_t read(uint8_t* bfr, uint16_t bfr_size);

  void init();

  // Celled from a task during initialization or from an ISR in case
  // of an RX error that requires restart.
  void start_rx_dma();

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
  void tx_next_chunk();

  // Called from isr to accept new incoming data from the RX DMA buffer.
  void rx_data_arrived_isr(const uint8_t* buffer, uint16_t len,
                           BaseType_t* task_woken);

  // _huart->error_code indicates the error code.
  // Search UART_Error_Definition for codes. Errors can
  // be soft, such as RX framing errors, so for now we
  // ignore them.
  void uart_error_isr();
};

namespace serial {
// Used by data link.
extern Serial serial1;
// Used by printer link.
extern Serial serial2;
}  // namespace serial

// #pragma GCC pop_options
