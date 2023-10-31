#pragma once

#include <FreeRTOS.h>

#include "queue.h"

// #pragma GCC push_options
// #pragma GCC optimize("O0")

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

  // The capacity of this queue.
  static constexpr uint16_t capacity = N;

  inline QueueHandle_t handle() { return _handle; }

  inline void reset() { xQueueReset(_handle); }

  // The current number of items in the queue.
  inline uint32_t size() { return uxQueueMessagesWaiting(_handle); }

  // Caller must call portYIELD_FROM_ISR(task_woken) at the very end of the ISR.
  inline bool add_from_isr(const T& item, BaseType_t* task_woken) {
    BaseType_t status = xQueueSendToBackFromISR(_handle, &item, task_woken);
    return status == pdPASS;
  }

  // Use portMAX_DELAY to indicate waiting forever. Use 0 to indicate no
  // blocking and immdiate failure if no space.
  inline bool add_from_task(const T& item, uint32_t timeout_millis) {
    static_assert(configTICK_RATE_HZ == 1000);
    const BaseType_t status = xQueueSendToBack(_handle, &item, timeout_millis);
    if (status == pdPASS) {
      return true;
    }
    // NOTE: errQUEUE_FULL doesn't really have a distinct error value.
    if (status == errQUEUE_FULL && timeout_millis != portMAX_DELAY) {
      // Timeout.
      return false;
    }
    // Everything else is a fatal error.
    error_handler::Panic(13);
  }

  // Use portMAX_DELAY to indicate waiting forever. Use 0 to indicate no
  // blocking and immdiate failure if no space.
  inline bool consume_from_task(T* item_buffer, uint32_t timeout_millis) {
    static_assert(configTICK_RATE_HZ == 1000);
    const BaseType_t status =
        xQueueReceive(_handle, item_buffer, timeout_millis);
    if (status == pdPASS) {
      return true;
    }
    // NOTE: errQUEUE_EMPTY doesn't really have a distinct error value.
    if (status == errQUEUE_EMPTY && timeout_millis != portMAX_DELAY) {
      // Timeout.
      return false;
    }
    // Everything else is a fatal error.
    error_handler::Panic(12);
  }

 private:
  uint8_t _items_mem[sizeof(T) * N];
  StaticQueue_t _queue_mem;
  QueueHandle_t const _handle;
};

// #pragma GCC pop_options
