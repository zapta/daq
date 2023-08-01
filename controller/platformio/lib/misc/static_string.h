#pragma once

#include <inttypes.h>

#include <cstring>

class AbstractStaticString {
 public:
  virtual bool set_c_str(const char* str) = 0;
  virtual bool set(const char* src, size_t src_size) = 0;
  virtual bool append(char c) = 0;
  virtual size_t max_len() const = 0;
  virtual void clear() = 0;
};

template <size_t N>
class StaticString : public AbstractStaticString {
 public:
  static constexpr size_t kMaxLen = N;

  virtual size_t max_len() const override { return kMaxLen; }

  virtual void clear() override {
    _len = 0;
    _buffer[_len] = 0;
  }

  virtual bool set(const char* src, size_t src_size) override {
    if (src_size > kMaxLen) {
      return false;
    }
    memcpy(_buffer, src, src_size);
    _len = src_size;
    _buffer[_len] = 0;
    return true;
  }

  virtual bool append(char c) override {
    if (is_full()) {
      return false;
    }
    _buffer[_len++] = c;
    _buffer[_len] = 0;
    return true;
  }

  virtual bool set_c_str(const char* str) override {
    const size_t str_len = strlen(str);
    return set(str, str_len);
  }

  const char* c_str() const { return _buffer; }

  size_t len() const { return _len; }

  bool is_empty() const { return _len == 0; }

  bool is_full() const { return _len >= kMaxLen; }

 private:
 // _buffer[_len] always has the null terminator.
  char _buffer[kMaxLen + 1] = {0};
  size_t _len = 0;
};