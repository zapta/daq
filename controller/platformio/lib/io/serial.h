// Serial driver. Interrupt driven, no worker tasks.
#pragma once

#include "FreeRTOS.h"
#include "circular_buffer.h"
#include "semphr.h"
#include "static_mutex.h"
#include "usart.h"
#include "time_util.h"

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
      MutexScope mutex_scope(_rx_mutext);
      __disable_irq();
      { result = _rx_buffer.size(); }
      __enable_irq();
    }
    return result;
  }

  // If blocking = true, blocks and return non zero count.
  uint16_t read(uint8_t* bfr, uint16_t len, bool blocking = true) {
    for (;;) {
      int bytes_read = 0;
      {
        MutexScope mutex_scope(_rx_mutext);
        __disable_irq();
        { bytes_read = _rx_buffer.read(bfr, len); }
        __enable_irq();
      }
      if (bytes_read || !blocking) {
        return bytes_read;
      }
      // Wait and try again.
      time_util::delay_millis(5);
    }
  }

  void init() {
    // Register callback handlers.
    if (HAL_OK != HAL_UART_RegisterCallback(_huart, HAL_UART_TX_COMPLETE_CB_ID,
                                            uart_TxCpltCallback)) {
      Error_Handler();
    }
    if (HAL_OK != HAL_UART_RegisterCallback(_huart, HAL_UART_RX_COMPLETE_CB_ID,
                                            uart_RxCpltCallback)) {
      Error_Handler();
    }
    if (HAL_OK !=
        HAL_UART_RegisterRxEventCallback(_huart, uart_RxEventCallback)) {
      Error_Handler();
    }
    // Start the reception.
    rx_next_chunk(0);
  }

 private:
  // For interrupt handling.
  static void uart_TxCpltCallback(UART_HandleTypeDef* huart);
  static void uart_RxCpltCallback(UART_HandleTypeDef* huart);
  static void uart_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size);

  UART_HandleTypeDef* _huart;
  // TX
  CircularBuffer<uint8_t, 5000> _tx_buffer;
  StaticMutex _tx_mutex;
  uint8_t _tx_transfer_buffer[20];
  // RX
  CircularBuffer<uint8_t, 5000> _rx_buffer;
  StaticMutex _rx_mutext;
  uint8_t _rx_transfer_buffer[20];

  // Called in mutex and in interrupt. No need to protect access.
  void tx_next_chunk() {
    const uint16_t len =
        _tx_buffer.read(_tx_transfer_buffer, sizeof(_tx_transfer_buffer));
    if (len > 0) {
      HAL_UART_Transmit_IT(_huart, _tx_transfer_buffer, len);
    }
  }

  // Called from an interrupt. Now need to protect access.
  void rx_next_chunk(uint16_t len) {
    if (len) {
      const bool ok = _rx_buffer.write(_rx_transfer_buffer, len, true);
      if (!ok) {
        asm("nop");
      }
    }
    const HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_IT(
        _huart, _rx_transfer_buffer, sizeof(_rx_transfer_buffer));
    if (status != HAL_OK) {
      asm("nop");
    }
  }
};

namespace serial {
// Used by data link.
extern Serial serial1;
// Used by printer link.
extern Serial serial2;
}  // namespace serial