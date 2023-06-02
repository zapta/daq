
#pragma once

#include <stdio.h>

#include <algorithm>

#include "main.h"
#include "stdarg.h"


enum LoggerLevel {
  LOG_VERBOSE = 1,
  LOG_INFO = 2,
  LOG_WARNING = 3,
  LOG_ERROR = 4,
  LOG_NONE = 5
};

class Logger {
 public:
  // enum Level { VERBOSE = 1, INFO = 2, WARNING = 3, ERROR = 4, NONE = 5 };

  Logger() : _level(LOG_INFO) {}

  Logger(LoggerLevel level)
      : _level(constrain_level(level)) {}

  // Setting to nullptr equivalent to no logging.
  // void set_stream(Stream* stream) { _optional_stream = stream; }

  void set_level(LoggerLevel level) {
    _level = constrain_level(level);
  }

  LoggerLevel level() { return _level; }

  inline bool is_level(LoggerLevel level) const {
    return level >= _level;
  }

  inline bool is_verbose() const { return is_level(LOG_VERBOSE); }

  inline bool is_info() const { return is_level(LOG_INFO); }

  inline bool is_warning() const { return is_level(LOG_WARNING); }

  inline bool is_error() const { return is_level(LOG_ERROR); }

  inline bool is_none() const { return is_level(LOG_NONE); }

  static LoggerLevel constrain_level(LoggerLevel level) {
    return std::max(LOG_VERBOSE, std::min(level, LOG_NONE));
  }

  // Log at verbose level.
  void verbose(const char* format, ...) const {
    if (is_verbose()) {
      va_list ap;
      va_start(ap, format);
      _vlog("V", format, ap);
      va_end(ap);
    }
  }

  // Log at info level.
  void info(const char* format, ...) const {
    if (is_info()) {
      va_list ap;
      va_start(ap, format);
      _vlog("I", format, ap);
      va_end(ap);
    }
  }

  // Log at warning level.
  void warning(const char* format, ...) const {
    if (is_warning()) {
      va_list ap;
      va_start(ap, format);
      _vlog("W", format, ap);
      va_end(ap);
    }
  }

  // Log at error level.
  void error(const char* format, ...) const {
    if (is_error()) {
      va_list ap;
      va_start(ap, format);
      _vlog("E", format, ap);
      va_end(ap);
    }
  }

 private:
  // Null if not availble. Not the owner of this pointer.
  // mutable Stream* _optional_stream = nullptr;

  LoggerLevel _level = LOG_VERBOSE;


  void _vlog(const char* level_str, const char* format, va_list args) const {
    // TODO: Consider to move from the stack to a static variable protected by 
    // semaphores.
   char bfr[100];
    vsnprintf(bfr, sizeof(bfr), format, args);
    printf("%s: %s\n", level_str, bfr);
  }
};

