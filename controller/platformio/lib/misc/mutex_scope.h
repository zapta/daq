#pragma once

#include <FreeRTOS.h>
#include <semphr.h>

// Protects the scope using a provided mutex.
class MutexScope {
 public:
  // Blocking. Waits to grab the mutex.
  inline MutexScope(SemaphoreHandle_t mutex_handle)
      : _mutex_handle(mutex_handle) {
    xSemaphoreTake(_mutex_handle, portMAX_DELAY);
  }
  // Release the mutex when exiting the scope.
  inline ~MutexScope() { xSemaphoreGive(_mutex_handle); }

  // Prevent copy and assignment.
  MutexScope(const MutexScope& other) = delete;
  MutexScope& operator=(const MutexScope& other) = delete;

 private:
  // The underlying mutex. Not an owner.
  SemaphoreHandle_t const _mutex_handle;
};
