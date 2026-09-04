#include "logger/async_logger.hpp"
#include <chrono>
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
  // log_file.flush (); We should avoid flushing every msg
}

AsyncLogger::AsyncLogger (std::string_view path,
                          std::function<void ()> cpu_affinity)
    : buffer_ (std::make_unique<MpscRingBuffer<LogRecord, kBufferCapacity>> ()),
      logger_ (std::make_unique<LoggerImpl> ()) {
  logger_->log_file.open (std::string (path), std::ios::out | std::ios::app);

  if (!logger_->log_file) {
    throw std::runtime_error ("Failed to open the log file");
  }

  logger_->consumer =
      std::jthread ([this, cpu_affinity] (std::stop_token stop_token) {
        if (cpu_affinity) {
          cpu_affinity ();
        }
        consume (stop_token);
      });
}

AsyncLogger::~AsyncLogger () = default;

auto AsyncLogger::dropped_count () const noexcept -> std::size_t {
  return dropped_count_.load (std::memory_order_relaxed);
}

void AsyncLogger::push_record (LogRecord&& record) {
  if (!buffer_->try_push (std::move (record))) {
    dropped_count_.fetch_add (1, std::memory_order_relaxed);
  }
}

void AsyncLogger::drain_buffer () {
  while (auto record = buffer_->try_pop ()) {
    logger_->write (*record);
  }
  logger_->log_file.flush ();
}

void AsyncLogger::consume (std::stop_token stop_token) {
  // TODO(known, deferred): logger_->write() can throw (disk full, format
  // error, etc.). Uncaught here, that's std::terminate() for the whole
  // process, not just a dropped line. Revisit if it actually bites during
  // benchmarking; deliberately left as-is for now.

  constexpr static auto kFlushInterval {std::chrono::microseconds {300}};
  auto next_flush = std::chrono::steady_clock::now () + kFlushInterval;

  while (!stop_token.stop_requested ()) {
    auto record = buffer_->try_pop ();

    if (!record) {
      std::this_thread::yield ();
      continue;
    }

    logger_->write (*record);

    const auto now = std::chrono::steady_clock::now ();
    if (now >= next_flush) {
      logger_->log_file.flush ();
      next_flush = now + kFlushInterval;
    }
  }

  drain_buffer ();
}

}  // namespace alll
