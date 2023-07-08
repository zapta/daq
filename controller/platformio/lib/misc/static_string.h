#pragma once

#include <inttypes.h>

#include <cstring>

class AbstractStaticString {
 public:
   virtual bool set_c_str(const char* str) = 0;
         virtual  bool set(const char* src, size_t src_size) = 0 ;
        virtual size_t max_len() const = 0;
      virtual void clear() = 0;


  // virtual void clear() = 0;
  // virtual  const char* c_str() const= 0;
  // virtual size_t len() const = 0;
  // virtual bool set(const char* src, size_t src_size)  = 0;
  // virtual bool set_c_str(const char* str)  = 0;
  // virtual vook is_empty()
};

template <size_t N>
class StaticString : public AbstractStaticString {
 public:
  static constexpr size_t kMaxLen = N;

     virtual size_t max_len() const override { return kMaxLen; }



   virtual void clear() override  { 
    _buffer[0] = 0; 
    }

   virtual bool set(const char* src, size_t src_size) override {
    if (src_size > kMaxLen) {
      clear();
      return false;
    }
    memcpy(_buffer, src, src_size);
    _buffer[src_size] = 0;
    return true;
  }

  virtual bool set_c_str(const char* str) override  {
    const size_t str_len = strlen(str);
    return set(str, str_len);
  }

   const char* c_str() const  { return _buffer; }

   size_t len() const  { return strlen(_buffer); }

   bool is_empty() const { return _buffer[0] != 0; }

 private:
  char _buffer[kMaxLen + 1] = {};
};