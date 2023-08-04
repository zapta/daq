#pragma once

#include "common.h"
#include <FreeRTOS.h>
#include <semphr.h>


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

  // Do not call from ISR. Timeout of portMAX_DELAY indicates
  // wait forever.
  inline bool take(TickType_t timeout_millis) {
    static_assert(configTICK_RATE_HZ == 1000);
    return xSemaphoreTake(_handle, timeout_millis);
  }

  inline void give() {
    const bool ok = xSemaphoreGive(_handle);
    if (!ok) {
      App_Error_Handler();
    }
  }

  inline void give_from_isr(BaseType_t* task_woken) {
    const bool ok = xSemaphoreGiveFromISR(_handle, task_woken);
    if (!ok) {
      App_Error_Handler();
    }
  }

 private:
  StaticSemaphore_t _semaphore_buffer;
  SemaphoreHandle_t const _handle;
};
