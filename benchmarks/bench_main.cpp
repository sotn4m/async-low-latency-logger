#include "latency_stats.hpp"
#include "logger/async_logger.hpp"
#include "logger/sync_logger.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

// TODO(milestone-7): #include <thread> once run_benchmark actually spawns
// producer threads (removed for now since an unused include is dead weight).

namespace {

// Milestone 7, first pass: ONE fixed configuration, proven correct, before
// generalizing into a sweep over thread counts / load levels. Tune these
// once the harness itself is trustworthy.
constexpr int kThreads = 4;
constexpr double kTargetRatePerThreadHz = 50'000.0; // offered load per producer thread
constexpr int kWarmupCalls = 1'000;                 // per thread, not recorded
constexpr int kMeasuredCalls = 100'000;              // per thread, after warmup

// Runs kThreads producer threads against `log_call` on an open-loop
// schedule and returns the merged per-call latency samples (warmup
// excluded). `log_call` is called as log_call(thread_id, call_index) and
// should do exactly one logger.log(...) call — nothing else — so the
// timed region measures only the logger's own cost.
//
// TODO(milestone-7):
//   Per thread:
//     1. Preallocate a local std::vector<std::chrono::nanoseconds> with
//        .reserve(kMeasuredCalls) BEFORE starting any timing — a
//        reallocation mid-run would contaminate a latency sample with
//        something that has nothing to do with the logger.
//     2. Run kWarmupCalls calls to log_call() first, unrecorded, so the
//        consumer thread (for async) and caches/branch predictors reach
//        a steady state before anything is measured.
//     3. Compute interval = 1s / kTargetRatePerThreadHz as a
//        std::chrono::duration. Track next_deadline, starting at
//        steady_clock::now(). For each of kMeasuredCalls calls:
//          - busy-wait or sleep until steady_clock::now() >= next_deadline
//          - time the call itself: t0 = steady_clock::now(); log_call(...);
//            t1 = steady_clock::now(); record (t1 - t0)
//          - next_deadline += interval — unconditionally, regardless of
//            how long the call took. This is what makes it open-loop:
//            the schedule doesn't slip just because one call was slow,
//            which is exactly the property that avoids coordinated
//            omission and lets you control offered load as an
//            independent variable.
//   Join all threads, concatenate their sample vectors into one, return it.
template <typename LogCall>
std::vector<std::chrono::nanoseconds> run_benchmark(LogCall&& log_call) {
  (void)log_call;
  return {};
}

} // namespace

int main() {
  {
    alll::SyncLogger logger("bench_sync.log");
    auto samples = run_benchmark(
        [&](int t, int i) { logger.log(alll::LogLevel::Info, "thread {} msg {}", t, i); });
    auto stats = alll::bench::LatencyStats::from_samples(samples);
    stats.print("sync");
  }

  {
    alll::AsyncLogger logger("bench_async.log");
    auto samples = run_benchmark(
        [&](int t, int i) { logger.log(alll::LogLevel::Info, "thread {} msg {}", t, i); });
    auto stats = alll::bench::LatencyStats::from_samples(samples);
    stats.print("async");
    std::printf("async dropped: %zu\n", logger.dropped_count());
  }

  return 0;
}
