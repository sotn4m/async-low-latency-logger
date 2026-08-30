#include "logger/sync_logger.hpp"
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace alll {

struct SyncLogger::LoggerImpl {
  std::ofstream log_file;
  std::mutex mutex;

  auto write (LogLevel level, std::string_view message) -> void;
};

auto SyncLogger::LoggerImpl::write (LogLevel level, std::string_view message)
    -> void {
  auto now = std::chrono::system_clock::now ();
  const auto log_message = std::format ("[{:%Y-%m-%d %H:%M:%S}] {}: {}\n", now,
                                        to_string_view (level), message);

  std::lock_guard lock (mutex);
  log_file << log_message;
  log_file.flush ();
}

SyncLogger::SyncLogger (std::string_view path)
    : logger_ (std::make_unique<LoggerImpl> ()) {
  logger_->log_file.open (std::string (path), std::ios::out | std::ios::app);

  if (!logger_->log_file) {
    throw std::runtime_error ("Failed to open the log file");
  }
}

SyncLogger::~SyncLogger () = default;

void SyncLogger::write_line (LogLevel level, std::string message) {
  logger_->write (level, message);
}

}  // namespace alll
