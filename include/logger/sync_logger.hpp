#pragma once

#include <format>
#include <memory>
#include <string_view>
#include <utility>

#include "logger/log_level.hpp"

namespace alll {

class SyncLogger {
 public:
  explicit SyncLogger (std::string_view path);
  ~SyncLogger ();

  SyncLogger (const SyncLogger&) = delete;
  SyncLogger& operator= (const SyncLogger&) = delete;

  template <typename... Args>
  void log (LogLevel level, std::format_string<Args...> fmt, Args&&... args);

 private:
  // Named differently from the public log() overloads on purpose: a
  // private member still participates in overload resolution even
  // though it can't be called from outside the class, and access control
  // is only checked *after* resolution picks a candidate — so if this
  // were also named log(), external calls that would have matched it
  // wouldn't fall back to the public template, they'd just fail with a
  // "private within this context" error. Different name sidesteps the
  // collision entirely.
  void write_line (LogLevel level, std::string message);
  struct LoggerImpl;
  std::unique_ptr<LoggerImpl> logger_;
};

template <typename... Args>
void SyncLogger::log (LogLevel level,
                      std::format_string<Args...> fmt,
                      Args&&... args) {
  write_line (level, std::format (fmt, std::forward<Args> (args)...));
}

}  // namespace alll
