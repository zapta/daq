#pragma once

#include <FreeRTOS.h>
#include <semphr.h>

#include "common.h"

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

  // Call from a thread only. Returns true if the semaphore changed
  // count from 0 to 1. Else, was already at 1.
  inline bool give() { return xSemaphoreGive(_handle); }

  // Call from interrupt handlers only. Returns true if the semaphore changed
  // count from 0 to 1. Else, was already at 1. 
  // Note that task_woken is commulative such that *task_woken is set
  // but never cleared.
  inline bool give_from_isr(BaseType_t* task_woken) {
    return xSemaphoreGiveFromISR(_handle, task_woken);
  }

 private:
  StaticSemaphore_t _semaphore_buffer;
  SemaphoreHandle_t const _handle;
};
