#pragma once

#include "FreeRTOS.h"
#include "circular_buffer.h"
#include "semphr.h"
#include "usart.h"

class Serial {
 public:
  Serial(UART_HandleTypeDef* huart) : _huart(huart) {}

  // // TODO: add a mutex for multiple writer threads.
  // void write(uint8_t* bfr, uint16_t len) {
  //   for (;;) {
  //     bool ok = false;
  //     bool tx_in_progress (from hurat)
  //     enter mutex
  //     __disable_irq();
  //     ok = _tx_buffer.write(bfr, len);
  //     __enable_irq();
  //     start_tx if needed.
  //     exit mutex
  //     if (ok) {
  //       return;
  //     }
  //     vTaskDelay(5);
  //   }
  // }

 private:
  UART_HandleTypeDef* _huart;
  CircularBuffer<uint8_t, 1000> _tx_buffer;
  // mutex

  // void irq_tx() {

  // }


};