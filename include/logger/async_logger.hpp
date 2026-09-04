#pragma once

#include <sched.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <stop_token>
#include <string_view>

#include "logger/log_level.hpp"
#include "logger/mpsc_ring_buffer.hpp"

namespace alll {

inline constexpr std::size_t kMessageCapacity = 200;
inline constexpr std::size_t kBufferCapacity =
    1uz << 18;  // 262144 slots (54MB). Was 1<<16 (65536, 13.5MB) — raised after
                // a sweep showed drop rate dropping from 3.76% to 0.00% at 6
                // threads and 30.28% to 5.10% at overload; see README Results.

struct LogRecord {
  std::chrono::system_clock::time_point timestamp;
  LogLevel level {LogLevel::Info};
  std::uint16_t length {0};
  std::array<char, kMessageCapacity> message {};
};

// Pins the calling thread to `cpu` via sched_setaffinity.
struct Affinity {
  std::size_t cpu;

  void operator() () const noexcept {
    cpu_set_t set;
    CPU_ZERO (&set);

    if (cpu >= CPU_SETSIZE) {
      return;
    }

    CPU_SET (cpu, &set);

    if (sched_setaffinity (0, sizeof (set), &set) != 0) {
      return;
    }
  }
};

class AsyncLogger {
 public:
  explicit AsyncLogger (std::string_view path,
                        std::function<void ()> cpu_affinity = {});
  ~AsyncLogger ();

  AsyncLogger (const AsyncLogger&) = delete;
  AsyncLogger& operator= (const AsyncLogger&) = delete;

  template <typename... Args>
  auto log (LogLevel level, std::format_string<Args...> fmt, Args&&... args)
      -> void;

  [[nodiscard]] auto dropped_count () const noexcept -> std::size_t;

 private:
  auto push_record (LogRecord&& record) -> void;
  auto drain_buffer () -> void;
  auto consume (std::stop_token stop_token) -> void;

  std::unique_ptr<MpscRingBuffer<LogRecord, kBufferCapacity>> buffer_;
  std::atomic<std::size_t> dropped_count_ {0};

  struct LoggerImpl;
  std::unique_ptr<LoggerImpl> logger_;
};

template <typename... Args>
auto AsyncLogger::log (LogLevel level,
                       std::format_string<Args...> fmt,
                       Args&&... args) -> void {
  std::array<char, kMessageCapacity> message {};
  auto result = std::format_to_n (message.begin (), message.size (), fmt,
                                  std::forward<Args> (args)...);

  const auto length = static_cast<std::uint16_t> (result.out - message.data ());

  LogRecord log_record {.timestamp = std::chrono::system_clock::now (),
                        .level = level,
                        .length = length,
                        .message = std::move (message)};

  push_record (std::move (log_record));
}

}  // namespace alll
