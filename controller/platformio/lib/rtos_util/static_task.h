#pragma once

#include <FreeRTOS.h>

#include "common.h"
#include "logger.h"
#include "task.h"

// Static tasks with given stack size is bytes.
template <uint16_t N>
class StaticTask {
 public:
  StaticTask(TaskFunction_t code, const char* const name, UBaseType_t priority)
      : _code(code), _name(name), _priority(priority) {}

  ~StaticTask() {
    // Static tasks should not be finalized.
    error_handler::Panic(85);
  }

  bool start() {
    if (_handle != nullptr) {
      logger.error("Task %s already started.", _name);
      
      return false;
    }
    _handle =
        xTaskCreateStatic(_code, _name, sizeof(_stack) / sizeof(_stack[0]),
                          nullptr, _priority, _stack, &_tcb);
    if (_handle == nullptr) {
      logger.error("Failed to start task %s.", _name);
      return false;
    }
    logger.info("Task %s started successfully", _name);
    return true;
  }

  inline TaskHandle_t handle() { return _handle; }

  uint32_t unused_stack_bytes() {
    return (_handle == nullptr)
               ? 0
               : sizeof(StackType_t) * uxTaskGetStackHighWaterMark(_handle);
  }

  // Prevent copy and assignment.
  StaticTask(const StaticTask& other) = delete;
  StaticTask& operator=(const StaticTask& other) = delete;

  // Ok to start/stop multiple times.
  void stop() {
    if (_handle == nullptr) {
      // Already stopped.
      return;
    }
    vTaskDelete(_handle);
    _handle = nullptr;
  }

 private:
  TaskFunction_t const _code;
  const char* const _name;
  const UBaseType_t _priority;

  TaskHandle_t _handle = nullptr;
  StaticTask_t _tcb;
  StackType_t _stack[N / sizeof(StackType_t)];
};
