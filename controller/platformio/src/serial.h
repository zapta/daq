#pragma once

#include "FreeRTOS.h"
#include "circular_buffer.h"
#include "semphr.h"
#include "usart.h"

class Serial {
 public:
  Serial(UART_HandleTypeDef* huart) : _huart(huart) {}

  void write_str(const char* str) { write((uint8_t*)str, strlen(str)); }

  // // TODO: add a mutex for multiple writer threads.
  void write(uint8_t* bfr, uint16_t len) {
    for (;;) {
      bool written = false;
      bool tx_in_progress = false;
      xSemaphoreTake(_tx_mutex, portMAX_DELAY);
      __disable_irq();
      written = _tx_buffer.write(bfr, len);
      tx_in_progress = _huart->gState & 0x01;
      __enable_irq();
      if (!tx_in_progress) {
        tx_next_chunk();
      }
      xSemaphoreGive(_tx_mutex);
      if (written) {
        return;
      }
      // Wait and try again.
      vTaskDelay(5);
    }
  }

  // Blocking. Return non zero count.
  uint16_t read(uint8_t* bfr, uint16_t len) {
    for (;;) {
      int bytes_read = 0;
      // bool tx_in_progress = false;
      xSemaphoreTake(_rx_mutex, portMAX_DELAY);
      __disable_irq();
      bytes_read = _rx_buffer.read(bfr, len);
      // tx_in_progress = _huart->gState & 0x01;
      __enable_irq();
      // if (!tx_in_progress) {
      //   tx_next_chunk();
      // }
      xSemaphoreGive(_rx_mutex);
      if (bytes_read) {
        return bytes_read;
      }
      // Wait and try again.
      vTaskDelay(5);
    }
  }

  void init() { rx_next_chunk(0); }

 private:
  friend void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart);
  friend void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart);
  friend void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart,
                                         uint16_t Size);

  UART_HandleTypeDef* _huart;
  // TX
  CircularBuffer<uint8_t, 1000> _tx_buffer;
  SemaphoreHandle_t _tx_mutex = xSemaphoreCreateMutex();
  uint8_t _tx_transfer_buffer[20];
  // RX
  CircularBuffer<uint8_t, 1000> _rx_buffer;
  SemaphoreHandle_t _rx_mutex = xSemaphoreCreateMutex();
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
extern Serial serial1;
}  // namespace serial