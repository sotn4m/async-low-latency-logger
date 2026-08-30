#pragma once

#include <string_view>

namespace alll {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

constexpr std::string_view to_string_view(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace:
      return "Trace";
    case LogLevel::Debug:
      return "Debug";
    case LogLevel::Info:
      return "Info";
    case LogLevel::Warn:
      return "Warn";
    case LogLevel::Error:
      return "Error";
  }
  return "Unknown";
}

} // namespace alll
