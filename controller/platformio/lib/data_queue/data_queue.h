#pragma once

#include <FreeRTOS.h>

#include "serial_packets_data.h"
#include "static_task.h"

namespace data_queue {

// A buffer made of a SerialPacketsData.
class DataBuffer {
 public:
  enum State { FREE, GRABBED, PENDING, PROCESSED };

  // Call init() before using.
  DataBuffer() {}

  // Disable copying and assignment.
  DataBuffer(const DataBuffer& other) = delete;
  DataBuffer& operator=(const DataBuffer& other) = delete;

  SerialPacketsData& packet_data() { return _packet_data; }
  const SerialPacketsData& packet_data() const { return _packet_data; }

  State state() const { return _state; }

 private:
  friend void data_queue::setup();
  friend void data_queue::data_queue_task_body_impl(void*);
  friend DataBuffer* data_queue::grab_buffer();
  friend void data_queue::queue_buffer(DataBuffer*);

  uint8_t _buffer_index = 0;
  State _state = FREE;
  SerialPacketsData _packet_data;

  void init(uint8_t buffer_index) {
    _buffer_index = buffer_index;
    _state = FREE;
    _packet_data.clear();
  }
};

void setup();

// Non blocking. Panic if a buffer is not available. Guaranteed to
// returned a non null value.
DataBuffer* grab_buffer();

// Non blocking.
void queue_buffer(DataBuffer* buffer);

void dump_state();

// Caller should provide a task to run this task body..
// Should be started after setup.
extern TaskBodyFunction data_queue_task_body;

}  // namespace data_queue