#include "latency_stats.hpp"
#include "logger/async_logger.hpp"
#include "logger/sync_logger.hpp"

#include <array>
#include <chrono>
#include <concepts>
#include <cstdio>
#include <filesystem>
#include <ranges>
#include <thread>
#include <vector>
namespace {

constexpr std::size_t kThreads = 4;
constexpr double kTargetRatePerThreadHz = 50'000.0;
constexpr std::size_t kWarmupCalls = 1'000;
constexpr std::size_t kMeasuredCalls = 100'000;

template <typename Logger>
std::vector<std::chrono::nanoseconds> run_benchmark (Logger& logger,
                                                     const auto period) {
  std::array<std::vector<std::chrono::nanoseconds>, kThreads> thread_samples;

  for (auto& samples : thread_samples) {
    samples.reserve (kMeasuredCalls);
  }

  auto producer = [&logger, period] (const auto thread_id, auto& samples) {
    for (const auto i : std::views::iota (0uz, kWarmupCalls)) {
      logger.log (alll::LogLevel::Info, "thread {} warmup {}", thread_id, i);
    }

    auto next_deadline = std::chrono::steady_clock::now ();

    for (const auto i : std::views::iota (0uz, kMeasuredCalls)) {
      while (std::chrono::steady_clock::now () < next_deadline) {
        // busy wait to measure latency at specifc throughput
      }

      const auto start = std::chrono::steady_clock::now ();
      logger.log (alll::LogLevel::Info, "thread {} msg {}", thread_id, i);
      const auto end = std::chrono::steady_clock::now ();

      samples.emplace_back (end - start);
      next_deadline += period;
    }
  };

  std::array<std::thread, kThreads> threads;
  for (const auto thread_id : std::views::iota (0uz, kThreads)) {
    threads[thread_id] = std::thread ([&producer, &thread_samples, thread_id] {
      producer (thread_id, thread_samples[thread_id]);
    });
  }

  for (auto& thread : threads) {
    thread.join ();
  }

  return thread_samples | std::views::join | std::ranges::to<std::vector> ();
}

}  // namespace

int main () {
  constexpr auto kPeriod =
      std::chrono::duration_cast<std::chrono::nanoseconds> (
          std::chrono::duration<double> {1.0 / kTargetRatePerThreadHz});

  auto benchmark = [kPeriod]<typename Logger> (const auto type) {
    auto log_file = std::format ("bench_{}.log", type);
    Logger logger (log_file);
    auto samples = run_benchmark (logger, kPeriod);

    std::optional<std::size_t> async_dropped_message;

    if constexpr (std::same_as<Logger, alll::AsyncLogger>) {
      async_dropped_message = logger.dropped_count ();
    }

    auto stats = alll::bench::LatencyStats::from_samples (
        samples, async_dropped_message);
    stats.print (type);
    std::filesystem::remove (log_file);
  };

  benchmark.operator()<alll::SyncLogger> ("sync");
  benchmark.operator()<alll::AsyncLogger> ("async");
  return 0;
}
