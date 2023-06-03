// Circular queue. Not thread safe.

#pragma once

#include <inttypes.h>

#include <algorithm>
#include <cstring>

template <class T, uint16_t N>
class CircularBuffer {
 public:
  // Capacity is assumed to be non zero.
  CircularBuffer() : _size(0), _start(0) {}

  // Disable copying and assignment, to avoid unintentinal overhead.
  // These buffers can be large.
  CircularBuffer(const CircularBuffer& other) = delete;
  CircularBuffer& operator=(const CircularBuffer& other) = delete;

  inline uint16_t size() { return _size; }
  inline uint16_t available_for_write() { return N - _size; }
  inline uint16_t capacity() { return N; }
  inline bool is_full() { return _size >= N; }
  inline bool is_empty() { return _size == 0; }

  void clear() {
    _size = 0;
    _start = 0;
  }

  // Perform after addition to a buffer index.
  inline void normalize_index(uint16_t& i) {
    if (i >= N) {
      i -= N;
    }
  }

  // Writes all items, deleting oldest one if buffer is full.
  // void enqueue(const T* bfr, uint16_t len);

  // // Returns min(size, len) items in bfr.
  // uint16_t dequeue(T* bfr, uint16_t len);

  bool write(const T* bfr, uint16_t len, bool overwrite = false) {
    if (overwrite) {
      // If len > N, consider only the last N items.
      if (len > N) {
        bfr += len - N;
        len = N;
      }
      // Here len <= N.
      // If needed, drops existing items to have room for <len> items.
      if (available_for_write() < len) {
        const uint16_t items_to_drop = _size + len - N;
        _size -= items_to_drop;
        _start += items_to_drop;
        normalize_index(_start);
      }
    } else {
      if (available_for_write() < len) {
        return false;
      }
    }

    // Here <len> <= free(). We add the data in at most two chunks.
    uint16_t items_added = 0;
    while (items_added < len) {
      const uint16_t limit1 = len - items_added;
      uint16_t dst = _start + _size;
      normalize_index(dst);
      const uint16_t limit2 = (dst < _start) ? _start - dst : N - dst;
      const uint16_t n = std::min(limit1, limit2);
      memcpy(&_buffer[dst], &bfr[items_added], n * sizeof(T));
      items_added += n;
      _size += n;
    }
    return true;
  }

  // Returns min(size, len) items in bfr.
  uint16_t read(T* bfr, uint16_t len) {
    const uint16_t items_to_transfer = std::min(len, _size);
    uint16_t items_transfered = 0;
    // Should iterate twice at most.
    while (items_transfered < items_to_transfer) {
      const uint16_t limit1 = items_to_transfer - items_transfered;
      const uint16_t limit2 = N - _start;
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
  T _buffer[N];
  uint16_t _size;
  uint16_t _start;
};