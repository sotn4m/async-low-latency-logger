#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <stop_token>
#include <string_view>

#include "logger/log_level.hpp"
#include "logger/mpsc_ring_buffer.hpp"

namespace alll {

inline constexpr std::size_t kMessageCapacity = 200;
inline constexpr std::size_t kRingCapacity = 1uz << 16; // 65536 slots — tune later; milestone 7's benchmark sweeps will want this configurable rather than fixed, revisit then.

// A fixed-size, POD-ish log entry — deliberately no std::string. `message`
// holds the already-formatted text (formatting happens on the producer's
// stack before this is built — see AsyncLogger::log below), truncated to
// kMessageCapacity if it doesn't fit. Fixed size means no heap allocation
// anywhere in this struct's lifetime, which is required for it to move
// through the ring buffer's try_push/try_pop without allocating.
struct LogRecord {
  std::chrono::system_clock::time_point timestamp;
  LogLevel level = LogLevel::Info;
  std::uint16_t length = 0; // actual bytes used in `message`; may be < kMessageCapacity
  std::array<char, kMessageCapacity> message{};
};

// Async, ring-buffer-backed logger. log() never blocks: no locks, no
// syscalls, no heap allocation. A single background thread drains the
// ring buffer and writes formatted records to disk.
//
// Overflow policy: if the ring buffer is full, the record is dropped and
// dropped_count() increments — the caller is never blocked waiting for
// space. That tradeoff (drop vs. block) is the whole point of comparing
// this against SyncLogger in the benchmark.
class AsyncLogger {
 public:
  explicit AsyncLogger(std::string_view path);
  ~AsyncLogger();

  AsyncLogger(const AsyncLogger&) = delete;
  AsyncLogger& operator=(const AsyncLogger&) = delete;

  // Hot path. Safe to call from any number of threads concurrently.
  //
  // TODO(milestone-4/5):
  //   1. Format `fmt`/`args` into a local std::array<char, kMessageCapacity>
  //      on the stack using std::format_to_n (not std::format — _n bounds
  //      the write so a long message truncates instead of overflowing;
  //      format_to_n_result tells you how many chars it actually wrote).
  //   2. Build a LogRecord: timestamp = system_clock::now(), the given
  //      level, the written length, and the buffer (or a copy of the
  //      written portion — decide whether it's simpler to always copy
  //      the full fixed array or just the `length` prefix).
  //   3. Call push_record(std::move(record)) — that's the non-template
  //      part below, kept out of the header on purpose (see its comment).
  template <typename... Args>
  void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args);

  // Monitoring: total messages dropped (buffer-full) since construction.
  // Safe to call from any thread. This is a pure counter nothing else's
  // visibility depends on — same reasoning as the ring buffer's write_
  // CAS: memory_order_relaxed is correct here, not just convenient.
  [[nodiscard]] std::size_t dropped_count() const noexcept;

 private:
  // Non-template: does ring_.try_push(record); on failure, increments
  // dropped_count_ instead of retrying or blocking. Split out from log()
  // so the ring-buffer/drop-counter logic isn't duplicated per Args...
  // instantiation, and so it can live in the .cpp instead of the header.
  //
  // TODO(milestone-4/5): implement.
  void push_record(LogRecord&& record);

  // TODO(milestone-5): the consumer loop, run on a std::jthread:
  //   - Loop: try_pop() from ring_. If it returns a record, format it to
  //     text (level, timestamp, message) and write it to the output file.
  //     If empty, back off briefly rather than spinning a full core for
  //     nothing — worth experimenting with a short spin count before
  //     falling back to std::this_thread::yield() or a small sleep_for,
  //     and noting in the benchmark writeup what you chose and why (it's
  //     a real latency-vs-CPU-usage tradeoff, not a free choice).
  //   - On stop_token.stop_requested(), drain whatever's left in the
  //     ring buffer before returning, so a clean shutdown doesn't lose
  //     already-enqueued messages.
  //   - Flush policy is a real design decision here too: flushing after
  //     every write mirrors SyncLogger's honest baseline, but batching
  //     flushes (e.g. every N records, or every few ms) costs producers
  //     nothing since it's off the hot path — that's part of *why* async
  //     logging can beat sync by more than "just moved the mutex," and
  //     worth calling out explicitly when you write up the results.
  void consume(std::stop_token stop_token);

  MpscRingBuffer<LogRecord, kRingCapacity> ring_;
  std::atomic<std::size_t> dropped_count_{0};

  // TODO(milestone-5): std::ofstream for the output file, opened in the
  // constructor (append mode, same reasoning as SyncLogger).
  // TODO(milestone-5): std::jthread running consume() — start it last in
  // the constructor, after everything consume() touches is initialized.
  // jthread's destructor requests stop and joins automatically, which is
  // what gives AsyncLogger's own destructor a clean shutdown for free.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

template <typename... Args>
void AsyncLogger::log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
  // TODO(milestone-4/5): see the numbered steps in the declaration's
  // comment above.
  (void)level;
  (void)fmt;
  ((void)args, ...);
}

} // namespace alll
