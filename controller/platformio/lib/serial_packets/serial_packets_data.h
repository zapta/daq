// A class with bytes buffer and data serialization/desertailization.
// Buffer size is fixed at build time to allow static allocation of RAM.
// Three different sizes are provided using template specialization.

#pragma once

#include <cstring>

#include "logger.h"
#include "serial_packets_consts.h"
#include "serial_packets_crc.h"
#include "static_string.h"

template <uint16_t N>
class SerialPacketsBuffer {
 public:
  SerialPacketsBuffer() {}
  ~SerialPacketsBuffer() {}

  // Disable copying and assignment.
  SerialPacketsBuffer(const SerialPacketsBuffer& other) = delete;
  SerialPacketsBuffer& operator=(const SerialPacketsBuffer& other) = delete;

  static inline uint16_t capacity() { return sizeof(_buffer); }
  inline uint16_t size() const { return _size; }
  inline uint16_t bytes_to_read() const { return _size - _bytes_read; }
  inline bool all_read() const { return _bytes_read >= _size; }
  inline bool had_read_errors() const { return _had_read_errors; }
  inline bool had_write_errors() const { return _had_write_errors; }
  inline uint16_t bytes_read() const { return _bytes_read; }
  inline uint16_t unread_bytes() const { return _size - _bytes_read; }
  inline uint16_t free_bytes() const { return sizeof(_buffer) - _size; }
  inline bool is_full() const { return _size >= sizeof(_buffer); }
  inline bool is_empty() const { return _size == 0; }

  inline bool all_read_ok() const { return all_read() && !had_read_errors(); }

  void clear() {
    _size = 0;
    _had_write_errors = false;
    reset_reading();
  }

  inline void reset_reading() const {
    _bytes_read = 0;
    _had_read_errors = false;
  }

  // Write content to a serial port or another stream.
  void dump(const char* title) const {
    logger.info(
        "%s\n"
        "  size: %hu\n"
        "  bytes_read: %hu\n"
        "  had_read_errros: %hu\n"
        "  capacity: %hu",
        title, _size, _bytes_read, _had_read_errors, capacity());
    // TODO: dump _data bytes in hex
  }

  // Compute the data's CRC.
  uint16_t crc16() const { return serial_packets_gen_crc16(_buffer, _size); }

  void write_uint8(uint8_t v) {
    if (_had_write_errors || 1 > free_bytes()) {
      _had_write_errors = true;
      return;
    }
    _buffer[_size++] = v;
  }

  void write_uint16(uint16_t v) {
    if (_had_write_errors || 2 > free_bytes()) {
      _had_write_errors = true;
      return;
    }
    uint8_t* p = &_buffer[_size];
    p[0] = v >> 8;
    p[1] = v >> 0;
    _size += 2;
  }

  void write_uint32(uint32_t v) {
    if (_had_write_errors || 4 > free_bytes()) {
      _had_write_errors = true;
      return;
    }
    uint8_t* p = &_buffer[_size];
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v >> 0;
    _size += 4;
  }

  void write_bytes(const uint8_t bytes[], uint32_t num_bytes) {
    if (_had_write_errors || num_bytes > free_bytes()) {
      _had_write_errors = true;
      return;
    }
    memcpy(&_buffer[_size], bytes, num_bytes);
    _size += num_bytes;
  }

  // Encode <len_byte><bytes...>
  // TODO: Add unit tests.
  // TODO: Add a similar method to write static string (len is known);
  void write_str(const char* str) {
    const uint32_t n = strlen(str);
    if (_had_write_errors || n > 255 || (1 + n) > free_bytes()) {
      _had_write_errors = true;
      return;
    }
    _buffer[_size++] = (uint8_t)n;
    memcpy(&_buffer[_size], str, n);
    _size += n;
  }

  uint8_t read_uint8() const {
    if (_had_read_errors || 1 > unread_bytes()) {
      _had_read_errors = true;
      return 0;
    }
    return _buffer[_bytes_read++];
  }

  uint16_t read_uint16() const {
    if (_had_read_errors || 2 > unread_bytes()) {
      _had_read_errors = true;
      return false;
    }
    const uint8_t* p = _buffer + _bytes_read;
    _bytes_read += 2;

    return ((uint16_t)p[0] << 8) | ((uint16_t)p[1] << 0);
  }

  uint32_t read_uint32() const {
    if (_had_read_errors || 4 > unread_bytes()) {
      _had_read_errors = true;
      return 0;
    }
    const uint8_t* p = _buffer + _bytes_read;
    _bytes_read += 4;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 0);
  }

  void read_bytes(uint8_t bytes_buffer[], uint32_t bytes_to_read) const {
    if (_had_read_errors || bytes_to_read > unread_bytes()) {
      memset(bytes_buffer, 0, bytes_to_read);
      _had_read_errors = true;
      return;
    }
    memcpy(bytes_buffer, &_buffer[_bytes_read], bytes_to_read);
    _bytes_read += bytes_to_read;
  }

  // TODO: Add unit tests.
  void read_str(AbstractStaticString* str) const {
    str->clear();

    // Pre conditions for reading the length byte.
    if (_had_read_errors || unread_bytes() < 1) {
      _had_read_errors = true;
      return;
    }

    // Read length byte and check preconditions for reading
    // the string bytes.
    const uint16_t n = (uint16_t)_buffer[_bytes_read++];
    if (n > unread_bytes() || n > str->max_len()) {
      _had_read_errors = true;
      return;
    }

    // Read the string bytes. Since we verified sizes earlier.
    // we don't expect an error status here..
    str->set((char*)&_buffer[_bytes_read], n);
    _bytes_read += n;
  }

  void skip_bytes(uint32_t bytes_to_skip) const {
    if (_had_read_errors || bytes_to_skip > unread_bytes()) {
      _had_read_errors = true;
      return;
    }
    _bytes_read += bytes_to_skip;
  }

  // Copy from a same size buffer.
  void copy_from(const SerialPacketsBuffer<N>& other) {
    clear();
    memcpy(_buffer, other._buffer, other._size);
    _size = other._size;
  }

 private:
  // The encoder and decoder access internal functionality for perofrmance.
  friend class SerialPacketsEncoder;
  friend class SerialPacketsClient;

  uint16_t _size = 0;
  uint8_t _buffer[N];

  mutable uint16_t _bytes_read = 0;
  mutable bool _had_read_errors = false;
  mutable bool _had_write_errors = false;
};

// For packet payload data only.
typedef SerialPacketsBuffer<MAX_PACKET_DATA_LEN> SerialPacketsData;

// For encoded packets, before stuffing and flagging. Adding two bytes for pre
// and post flags.
typedef SerialPacketsBuffer<serial_packets_consts::MAX_PACKET_LEN + 2>
    EncodedPacketBuffer;

// For encoded packets, after stuffing and flagging (wire format).
typedef SerialPacketsBuffer<serial_packets_consts::MAX_STUFFED_PACKET_LEN>
    StuffedPacketBuffer;