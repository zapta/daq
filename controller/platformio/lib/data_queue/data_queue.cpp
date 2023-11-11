#include "data_queue.h"

#include "data_recorder.h"
#include "error_handler.h"
#include "gpio_pins.h"
#include "host_link.h"
#include "static_mutex.h"
#include "static_queue.h"

// #pragma GCC push_options
// #pragma GCC optimize("O0")

namespace data_queue {

static constexpr int kNumBuffers = 4;
static DataBuffer data_buffers[kNumBuffers];
static StaticQueue<uint8_t, kNumBuffers> free_buffers_indexes_queue;
static StaticQueue<uint8_t, kNumBuffers> pending_buffers_indexes_queue;
static bool setup_completed = false;

// Static variables and a mutex to protect them.
static StaticMutex mutex;
static uint8_t min_free_queue_size = kNumBuffers;
static uint8_t max_pending_queue_size = 0;

void setup() {
  static_assert(kNumBuffers == free_buffers_indexes_queue.capacity);
  static_assert(kNumBuffers == pending_buffers_indexes_queue.capacity);

  // Sanity check. Called only once.
  if (setup_completed) {
    error_handler::Panic(56);
  }

  // Make all the buffers free.
  static_assert(kNumBuffers == sizeof(data_buffers) / sizeof(data_buffers[0]));
  for (uint8_t i = 0; i < kNumBuffers; i++) {
    data_buffers[i].init(i);
    if (!free_buffers_indexes_queue.add_from_task(i, 0)) {
      error_handler::Panic(15);
    }
    // No need to use the mutex during initialization.
    // min_free_queue_size++;
  }

  // min_free_queue_size = k

  setup_completed = true;
}

// Not 'static' to allow declaration as a friend.
void data_queue_task_body(void* ignored_argument) {
  if (!setup_completed) {
    error_handler::Panic(57);
  }

  for (;;) {
    // Wait for next pending buffer.
    uint8_t buffer_index = -1;
    if (!pending_buffers_indexes_queue.consume_from_task(&buffer_index,
                                                         portMAX_DELAY)) {
      error_handler::Panic(16);
    }
    // logger.info("data_buffer[%hu]: procesing", buffer_index);

    // Get buffer address.
    if (buffer_index >= kNumBuffers) {
      error_handler::Panic(17);
    }
    DataBuffer& buffer = data_buffers[buffer_index];
    if (buffer._state != DataBuffer::PENDING) {
      error_handler::Panic(18);
    }

    // Send data to monitor.
    gpio_pins::TEST1.set_high();
    host_link::client.sendMessage(host_link::HostPorts::LOG_REPORT_MESSAGE,
                                  buffer.packet_data());
    // Send data to SD.
    data_recorder::append_log_record_if_recording(buffer.packet_data());
    gpio_pins::TEST1.set_low();

    // Free the buffer.
    // logger.info("data_buffer[%hu]: freeing", buffer_index);
    buffer._state = DataBuffer::FREE;
    // We don't expect blocking here.
    if (!free_buffers_indexes_queue.add_from_task(buffer_index, 0)) {
      error_handler::Panic(19);
    }
  }
}

// Returned a non null value.
DataBuffer* grab_buffer() {
  uint8_t buffer_index = -1;

  // Grab the next free buffer, no blocking, and track min free buffers.
  {
    MutexScope scope(mutex);

    // Since this is non blocking it's ok to do within the mutex.
    if (!free_buffers_indexes_queue.consume_from_task(&buffer_index, 0)) {
      error_handler::Panic(21);
    }

    // Track min free items.
    const uint8_t current_free = free_buffers_indexes_queue.size();
    if (current_free < min_free_queue_size) {
      min_free_queue_size = current_free;
    }
  }

  // Sanity check.
  if (buffer_index >= kNumBuffers) {
    error_handler::Panic(22);
  }
  DataBuffer* buffer = &data_buffers[buffer_index];
  if (buffer->state() != DataBuffer::FREE) {
    error_handler::Panic(23);
  }
  buffer->_state = DataBuffer::GRABBED;
  return buffer;
}

void queue_buffer(DataBuffer* buffer) {
  const uint8_t buffer_index = buffer->_buffer_index;
  // Sanity check.
  if (buffer_index >= kNumBuffers) {
    error_handler::Panic(24);
  }
  if (buffer != &data_buffers[buffer_index]) {
    error_handler::Panic(54);
  }
  if (buffer->state() != DataBuffer::GRABBED) {
    error_handler::Panic(25);
  }
  buffer->_state = DataBuffer::PENDING;

  // Queue the buffer index and track max number of pending items.
  {
    MutexScope scope(mutex);
    // Since this is non blocking, it's OK do do within the mutex.
    if (!pending_buffers_indexes_queue.add_from_task(buffer_index, 0)) {
      error_handler::Panic(26);
    }
    // Track max pending queue size.
    const uint8_t current_pending = pending_buffers_indexes_queue.size();
    if (current_pending > max_pending_queue_size) {
      max_pending_queue_size = current_pending;
    }
  }
}

void dump_state() {
  // Get values within a mutex
  uint8_t min_free = 0;
  uint8_t max_pending = 0;
  {
    MutexScope scope(mutex);

    min_free = min_free_queue_size;
    max_pending = max_pending_queue_size;
  }

  // Report values. We lax here with the atomicity of the current
  // queue sizes but this is good enough for this dignostics function.
  logger.info("data_queue: free: %lu(%hu), pending = %lu(%hu)",
              free_buffers_indexes_queue.size(), min_free,
              pending_buffers_indexes_queue.size(), max_pending);
}

// The exported runnable.
StaticRunnable data_queue_task_runnable(data_queue_task_body, nullptr);

}  // namespace data_queue