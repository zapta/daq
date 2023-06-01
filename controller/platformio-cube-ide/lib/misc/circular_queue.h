// Circular queue. Not thread safe.

#pragma once

#include <inttypes.h>
#include <algorithm>
#include <cstring>

template <typename T>
class CircularQueue {
 public:
  // Capacity is assumed to be non zero.
  CircularQueue(T* buffer, uint16_t capacity)
      : _buffer(buffer), _capacity(capacity), _size(0), _start(0) {}

  // Disable copying and assignment, to avoid unintentinal overhead.
  // These buffers can be large.
  CircularQueue(const CircularQueue& other) = delete;
  CircularQueue& operator=(const CircularQueue& other) = delete;

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

  // Writes all items, deleting oldest one if buffer is full.
  // void enqueue(const T* bfr, uint16_t len);

  // // Returns min(size, len) items in bfr.
  // uint16_t dequeue(T* bfr, uint16_t len);

  void enqueue(const T* bfr, uint16_t len) {
    // If len > capacity, consider only the last <capacity> items.
    if (len > _capacity) {
      bfr += len - _capacity;
      len = _capacity;
    }
    // Here len <= _capacity.
    // If needed, drops existing items to have room for <len> items.
    if (_capacity - _size < len) {
      const uint16_t items_to_drop = _size + len - _capacity;
      _size -= items_to_drop;
      _start += items_to_drop;
      normalize_index(_start);
    }
    // Add the <len> items, in at most two chunks.
    uint16_t items_added = 0;
    while (items_added < len) {
      const uint16_t limit1 = len - items_added;
      uint16_t dst = _start + _size;
      normalize_index(dst);
      const uint16_t limit2 = (dst < _start) ? _start - dst : _capacity - dst;
      const uint16_t n = std::min(limit1, limit2);
      memcpy(&_buffer[dst], &bfr[items_added], n * sizeof(T));
      items_added += n;
      _size += n;
    }
  }

  // Returns min(size, len) items in bfr.
  uint16_t dequeue(T* bfr, uint16_t len) {
    const uint16_t items_to_transfer = std::min(len, _size);
    uint16_t items_transfered = 0;
    // Should iterate twice at most.
    while (items_transfered < items_to_transfer) {
      const uint16_t limit1 = items_to_transfer - items_transfered;
      const uint16_t limit2 = _capacity - _start;
      const uint16_t n = std::min(limit1, limit2);
      memcpy(&bfr[items_transfered], &_buffer[_start], n * sizeof(T));
      items_transfered += n;
      _size -= n;
      _start += n;
      normalize_index(_start);
    }
    return items_to_transfer;
  }

 private:
  T* const _buffer;
  const uint16_t _capacity;
  uint16_t _size;
  uint16_t _start;
};