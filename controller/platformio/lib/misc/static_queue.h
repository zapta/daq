#pragma once

#include <FreeRTOS.h>
// #include <semphr.h>
// #include "main.h"
#include "queue.h"

template <typename T, uint16_t N>
class StaticQueue {
 public:
  StaticQueue()
      : _handle(xQueueCreateStatic(N, sizeof(T), _items_mem, &_queue_mem)) {}

  // This will typically be used in testing only.
  ~StaticQueue() { vQueueDelete(_handle); }

  // Prevent copy and assignment.
  StaticQueue(const StaticQueue& other) = delete;
  StaticQueue& operator=(const StaticQueue& other) = delete;

  inline QueueHandle_t handle() { return _handle; }

  inline void reset() { xQueueReset(_handle); }

  // Must call portYIELD_FROM_ISR(task_woken) at the very end of the ISR.
  inline bool add_from_isr(const T& item, BaseType_t* task_woken) {
    BaseType_t status = xQueueSendToBackFromISR(_handle, &item, task_woken);
    return status == pdPASS;
  }

  // The special timeout portMAX_DELAY indicates waiting forever.
  inline bool consume_from_task(T* item_buffer, uint32_t timeout_millis) {
    static_assert(configTICK_RATE_HZ == 1000);
    BaseType_t status = xQueueReceive(_handle, item_buffer, timeout_millis);
    return status == pdTRUE;
  }

 private:
  uint8_t _items_mem[sizeof(T) * N];
  StaticQueue_t _queue_mem;
  QueueHandle_t const _handle;
};
