#pragma once

#include <FreeRTOS.h>
#include <semphr.h>

// #include "main.h"

// A binary sempahore and its static memory.
class StaticBinarySemaphore {
 public:
  StaticBinarySemaphore()
      : _handle(xSemaphoreCreateBinaryStatic(&_semaphore_buffer)) {}

  // This will typically be used in testing only.
  ~StaticBinarySemaphore() { vSemaphoreDelete(_handle); }

  // Prevent copy and assignment.
  StaticBinarySemaphore(const StaticBinarySemaphore& other) = delete;
  StaticBinarySemaphore& operator=(const StaticBinarySemaphore& other) = delete;

  inline SemaphoreHandle_t handle() { return _handle; }

  inline void take(TickType_t xTicksToWait) {
    xSemaphoreTake(_handle, xTicksToWait);
  }

  inline void give() { xSemaphoreGive(_handle); }

  inline void give_from_isr(signed BaseType_t* pxHigherPriorityTaskWoken) {
    xSemaphoreGiveFromISR(_handle, pxHigherPriorityTaskWoken);
  }

 private:
  StaticSemaphore_t _semaphore_buffer;
  SemaphoreHandle_t const _handle;
};
