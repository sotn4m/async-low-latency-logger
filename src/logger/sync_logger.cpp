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
};

SyncLogger::SyncLogger (std::string_view path)
    : logger_ (std::make_unique<LoggerImpl> ()) {
  logger_->log_file.open (std::string (path), std::ios::out | std::ios::app);

  if (!logger_->log_file) {
    throw std::runtime_error ("Failed to open the log file");
  }
}

SyncLogger::~SyncLogger () = default;

void SyncLogger::log (LogLevel level, std::string_view message) {
  std::string_view log_level;

  switch (level) {
    case LogLevel::Trace:
      log_level = "Trace";
      break;
    case LogLevel::Debug:
      log_level = "Debug";
      break;
    case LogLevel::Info:
      log_level = "Info";
      break;
    case LogLevel::Warn:
      log_level = "Warn";
      break;
    case LogLevel::Error:
      log_level = "Error";
      break;
    default:
      log_level = "Unknown";
      break;
  }

  auto now = std::chrono::system_clock::now ();
  const auto log_message =
      std::format ("[{:%Y-%m-%d %H:%M:%S}] {}: {}\n", now, log_level, message);

  std::lock_guard lock (logger_->mutex);
  logger_->log_file << log_message;
  logger_->log_file.flush ();
}

}  // namespace alll
