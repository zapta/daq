#pragma once

#include <FreeRTOS.h>
#include <semphr.h>
#include "main.h"



// A mutex and its static memory.
class StaticMutex {
 public:
  StaticMutex() : _handle(xSemaphoreCreateMutexStatic(&_mutex_buffer)) {}

  // This will typically be used in testing only.
  ~StaticMutex() {
    vSemaphoreDelete(_handle);
  }

  // Prevent copy and assignment.
  StaticMutex(const StaticMutex& other) = delete;
  StaticMutex& operator=(const StaticMutex& other) = delete;

  inline SemaphoreHandle_t handle() { return _handle; }

  inline void take(TickType_t xTicksToWait) {
    xSemaphoreTake(_handle, xTicksToWait);
  }
  inline void give() { xSemaphoreGive(_handle); }

 private:
  StaticSemaphore_t _mutex_buffer;
  SemaphoreHandle_t const _handle;
};

// Protects the scope using a provided mutex.
class MutexScope {
 public:
  // Blocking. Waits to grab the mutex.
  inline MutexScope(StaticMutex& static_mutex)
      : _mutex_handle(static_mutex.handle()) {
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