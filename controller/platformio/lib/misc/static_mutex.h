#pragma once

#include <FreeRTOS.h>
#include <semphr.h>
#include "main.h"

// #include "logger.h"

// TODO: add a scoped disable interrupts.
// TODO: add a static task wrapper.

// A mutex and its static memory.
class StaticMutex {
 public:
  StaticMutex() : _handle(xSemaphoreCreateMutexStatic(&_mutex_buffer)) {}

  ~StaticMutex() {
    // Static mutexes should not be finalized.
    Error_Handler();
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

// // Static tasks with given stack size is bytes.
// template <uint16_t N>
// class StaticTask {
//  public:
//   // TaskFunction_t pxTaskCode,
//   // const char * const pcName,
//   // const uint32_t ulStackDepth,
//   // void * const pvParameters,
//   // UBaseType_t uxPriority,
//   // StackType_t * const puxStackBuffer,
//   // StaticTask_t * const pxTaskBuffer

//   StaticTask(TaskFunction_t code, const char* const name, UBaseType_t priority)
//       : _code(code), _name(name), _priority(priority) {}

//   ~StaticTask() {
//     // Static tasks should not be finalized.
//     Error_Handler();
//   }

//   bool start() {
//     if (_handle != nullptr) {
//       logger.error("Task %s already started.", _name);
//       // Already started.
//       return false;
//     }
//     _handle =
//         xTaskCreateStatic(_code, _name, sizeof(_stack) / sizeof(_stack[0]),
//                           nullptr, _priority, _stack, &_tcb);
//     if (_handle == nullptr) {
//       logger.error("Failed to start task %s.", _name);
//       return false;
//     }
//     logger.info("Task %s started successfully", _name);
//     return true;
//   }

//   inline TaskHandle_t handle() { return _handle; }

//   // This is non tested and vSemaphoreDelete may not work if tasks are waiting.
//   // Generally speaking we don't intend to free static mutexes.
//   // ~StaticTask() { logger.error("Finalizing static task %s", _name); }

//   // Prevent copy and assignment.
//   StaticTask(const StaticTask& other) = delete;
//   StaticTask& operator=(const StaticTask& other) = delete;

//   // inline SemaphoreHandle_t handle() { return _handle; }

//   // inline void take(TickType_t xTicksToWait) {
//   //   xSemaphoreTake(_handle, xTicksToWait);
//   // }
//   // inline void give() { xSemaphoreGive(_handle); }

//  private:
//   TaskFunction_t const _code;
//   const char* const _name;
//   const UBaseType_t _priority;

//   TaskHandle_t _handle = nullptr;
//   StaticTask_t _tcb;
//   StackType_t _stack[N / sizeof(StackType_t)];

//   // StaticSemaphore_t _mutex_buffer;
//   // SemaphoreHandle_t const _handle;
// };
