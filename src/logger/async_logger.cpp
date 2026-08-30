#include "logger/async_logger.hpp"
#include <fstream>
#include <string_view>
#include <thread>

namespace alll {

struct AsyncLogger::LoggerImpl {
  std::ofstream log_file;
  std::jthread consumer;

  auto write (const LogRecord& log) -> void;
};

auto AsyncLogger::LoggerImpl::write (const LogRecord& log) -> void {
  const auto level = to_string_view (log.level);
  const auto message_view = std::string_view (log.message.data (), log.length);

  const auto log_message = std::format ("[{:%Y-%m-%d %H:%M:%S}] {}: {}\n",
                                        log.timestamp, level, message_view);
  log_file << log_message;
  log_file.flush ();  // We should not flush on every message; change later
}

AsyncLogger::AsyncLogger (std::string_view path)
    : logger_ (std::make_unique<LoggerImpl> ()) {
  logger_->log_file.open (std::string (path), std::ios::out | std::ios::app);

  if (!logger_->log_file) {
    throw std::runtime_error ("Failed to open the log file");
  }

  logger_->consumer = std::jthread (
      [this] (std::stop_token stop_token) { consume (stop_token); });
}

AsyncLogger::~AsyncLogger () = default;

std::size_t AsyncLogger::dropped_count () const noexcept {
  return dropped_count_.load (std::memory_order_relaxed);
}

void AsyncLogger::push_record (LogRecord&& record) {
  if (!buffer_.try_push (std::move (record))) {
    dropped_count_.fetch_add (1, std::memory_order_relaxed);
  }
}

void AsyncLogger::drain_buffer () {
  while (auto record = buffer_.try_pop ()) {
    logger_->write (*record);
  }
}

void AsyncLogger::consume (std::stop_token stop_token) {
  // TODO(known, deferred): logger_->write() can throw (disk full, format
  // error, etc.). Uncaught here, that's std::terminate() for the whole
  // process, not just a dropped line. Revisit if it actually bites during
  // benchmarking; deliberately left as-is for now.
  while (!stop_token.stop_requested ()) {
    auto record = buffer_.try_pop ();

    if (!record) {
      std::this_thread::yield ();
      continue;
    }

    logger_->write (*record);
  }

  drain_buffer ();
}

}  // namespace alll
