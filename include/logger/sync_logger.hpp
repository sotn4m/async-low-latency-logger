#pragma once

#include <memory>
#include <string_view>

#include "logger/log_level.hpp"

namespace alll {

class SyncLogger {
 public:
  explicit SyncLogger (std::string_view path);
  ~SyncLogger ();

  SyncLogger (const SyncLogger&) = delete;
  SyncLogger& operator= (const SyncLogger&) = delete;

  void log (LogLevel level, std::string_view message);

 private:
  struct LoggerImpl;
  std::unique_ptr<LoggerImpl> logger_;
};

}  // namespace alll
