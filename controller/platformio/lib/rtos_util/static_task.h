#pragma once

#include <FreeRTOS.h>

#include "common.h"
#include "logger.h"
#include "task.h"

// #pragma GCC push_options
// #pragma GCC optimize("O0")

// Abstraction of a task body.
class TaskBody {
 public:
  TaskBody(){}
  // Prevent copy and assignment.
  TaskBody(const TaskBody& other) = delete;
  TaskBody& operator=(const TaskBody& other) = delete;
  // Subclasses should implement this.
  virtual void task_body() = 0;
};

// Implementation of TaskBody that makes a C function TaskBody.
class TaskBodyFunction : public TaskBody {
 public:
  TaskBodyFunction(TaskFunction_t task_function, void* const pvParameters)
      : _task_function(task_function), _pvParameters(pvParameters) {}
  virtual void task_body() { _task_function(_pvParameters); }

 private:
  TaskFunction_t const _task_function;
  void* const _pvParameters;
};

// Static tasks with given stack size is bytes.
class StaticTask {
 public:

  StaticTask(TaskBody& runnable, const char* const name, UBaseType_t priority)
      : _runnable(runnable), _name(name), _priority(priority) {}

  ~StaticTask() {
    // Static tasks should not be finalized.
    error_handler::Panic(85);
  }

  // Prevent copy and assignment.
  StaticTask(const StaticTask& other) = delete;
  StaticTask& operator=(const StaticTask& other) = delete;

bool start() {
  if (_handle != nullptr) {
    return false;
  }

  // Passing this StaticTask object as the private parameter to allow
  // dispatching to the runable.
  _handle = xTaskCreateStatic(runnable_dispatcher, _name,
                              sizeof(_stack) / sizeof(_stack[0]), this,
                              _priority, _stack, &_tcb);
  if (_handle == nullptr) {
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


 private:
  // We use the same stack size for all tasks.
  static constexpr uint32_t kStackSizeInBytes = 2000;
  static constexpr uint32_t kStackSizeInStackType = kStackSizeInBytes / sizeof(StackType_t);


  TaskBody&  _runnable;
  const char* const _name;
  const UBaseType_t _priority;

  TaskHandle_t _handle = nullptr;
  StaticTask_t _tcb;
  StackType_t _stack[kStackSizeInStackType];

  // A shared FreeRTOS task body that dispatches to the runnable.
  static void runnable_dispatcher(void* pvParameters) {
    StaticTask* const static_task = (StaticTask*)pvParameters;
    // Not expected to return.
    (static_task->_runnable).task_body();
  }
};

// #pragma GCC pop_options
