
#include "circular_byte_buffer.h"

#include <algorithm>
#include <cstring>

// class CircularByteBuffer {
//  public:
//   CircularByteBuffer(uint8_t* buffer, uint16_t capacity) {}

//   // Disable copying and assignment, to avoid unintentinal overhead.
//   // These buffers can be large.
//   CircularByteBuffer(const CircularByteBuffer& other) = delete;
//   CircularByteBuffer& operator=(const CircularByteBuffer& other) = delete;

//   uint16_t size() { return _size; }
//   uint16_t capacity() { return _capacity; }
//   bool is_full() { return _size >= _capacity; }
//   bool is_empty() { return _size == 0; }

// Writes all bytes, deleting oldest one if buffer is full.

void CircularByteBuffer::enqueue(const uint8_t* bfr, uint16_t len) {
  // If len > capacity, consider only the last <capacity> bytes.
  if (len > _capacity) {
    bfr += len - _capacity;
    len = _capacity;
  }
  // Here len <= _capacity. 
  // If needed, drops existing bytes to have room for <len> bytes.
  if (_capacity - _size < len) {
    const uint16_t bytes_to_drop = _size + len - _capacity;
    _size -= bytes_to_drop;
    _start += bytes_to_drop;
    normalize_index(_start);
  }
  // Add the <len> bytes, in at most two chunks.
  uint16_t bytes_added = 0;
  while (bytes_added < len) {
    const uint16_t limit1 = len - bytes_added;
    uint16_t dst = _start + _size;
    normalize_index(dst);
    const uint16_t limit2 = (dst < _start) ? _start - dst : _capacity - dst;
    const uint16_t n = std::min(limit1, limit2);
    memcpy(&_buffer[dst], &bfr[bytes_added], n);
    bytes_added += n;
    _size += n;
  }
}

// Returns min(size, len) bytes in bfr.
uint16_t CircularByteBuffer::dequeue(uint8_t* bfr, uint16_t len) {
  const uint16_t bytes_to_transfer = std::min(len, _size);
  uint16_t bytes_transfered = 0;
  // Should iterate twice at most.
  while (bytes_transfered < bytes_to_transfer) {
    const uint16_t limit1 = bytes_to_transfer - bytes_transfered;
    const uint16_t limit2 = _capacity - _start;
    const uint16_t n = std::min(limit1, limit2);
    memcpy(&bfr[bytes_transfered], &_buffer[_start], n);
    bytes_transfered += n;
    _size -= n;
    _start += n;
    normalize_index(_start);
  }
  return bytes_to_transfer;
}

//  private:
//   uint8_t* const _buffer;
//   const uint16_t _capacity;
//   const uint16_t _size;
//   const uint16_t _start;
// };