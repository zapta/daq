// Circular byte buffer. Not thread safe.

#pragma once

#include <inttypes.h>

class CircularByteBuffer {
 public:
  // Capacity is assumed to be non zero.
  CircularByteBuffer(uint8_t* buffer, uint16_t capacity)
      : _buffer(buffer), _capacity(capacity), _size(0), _start(0) {}

  // Disable copying and assignment, to avoid unintentinal overhead.
  // These buffers can be large.
  CircularByteBuffer(const CircularByteBuffer& other) = delete;
  CircularByteBuffer& operator=(const CircularByteBuffer& other) = delete;

  uint16_t size() { return _size; }
  uint16_t capacity() { return _capacity; }
  bool is_full() { return _size >= _capacity; }
  bool is_empty() { return _size == 0; }

  void clear() {
    _size = 0;
    _start = 0;
  }

  // Perform after addition to a buffer index.
  inline void normalize_index(uint16_t& i) {
    if (i >= _capacity) {
      i -= _capacity;
    }
  }

  // Writes all bytes, deleting oldest one if buffer is full.
  void enqueue(const uint8_t* bfr, uint16_t len);

  // Returns min(size, len) bytes in bfr.
  uint16_t dequeue(uint8_t* bfr, uint16_t len);

 private:
  uint8_t* const _buffer;
  const uint16_t _capacity;
  uint16_t _size;
  uint16_t _start;
};