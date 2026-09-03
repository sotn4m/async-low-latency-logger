#include "latency_stats.hpp"
#include "logger/async_logger.hpp"
#include "logger/sync_logger.hpp"

#include <array>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct BenchmarkConfig {
  constexpr static double target_rate_hz {50'000.0};
  constexpr static std::size_t warmup_calls {1'000uz};
  constexpr static std::size_t measured_calls {100'000uz};
  constexpr static std::size_t overload_thread_count {8uz};

  // core 0 is left for the OS, core 1 for AsyncLogger's consumer thread,
  // cores 2-7 for producers.
  constexpr static std::size_t consumer_cpu {1uz};
  constexpr static std::size_t producer_cpu_base {2uz};
  constexpr static std::array thread_counts {1uz, 2uz, 4uz, 6uz};

  [[nodiscard]] constexpr auto period () const {
    return std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::duration<double> {1.0 / target_rate_hz});
  }
};

constexpr BenchmarkConfig kConfig {};

template <typename Logger>
auto run_producer (Logger& logger,
                   std::size_t cpu,
                   std::barrier<>& start_barrier)
    -> std::vector<std::chrono::nanoseconds> {
  alll::Affinity {cpu}();

  std::thread::id thread_id = std::this_thread::get_id ();

  std::vector<std::chrono::nanoseconds> samples;
  samples.reserve (kConfig.measured_calls);

  for (const auto i : std::views::iota (0uz, kConfig.warmup_calls)) {
    logger.log (alll::LogLevel::Info, "thread {} warmup {}", thread_id, i);
  }

  start_barrier.arrive_and_wait ();

  auto next_deadline = std::chrono::steady_clock::now () + kConfig.period ();

  for (const auto i : std::views::iota (0uz, kConfig.measured_calls)) {
    while (std::chrono::steady_clock::now () < next_deadline) {
      // busy wait to hold the open-loop schedule
    }

    const auto start = std::chrono::steady_clock::now ();
    logger.log (alll::LogLevel::Info, "thread {} msg {}", thread_id, i);
    const auto end = std::chrono::steady_clock::now ();

    samples.emplace_back (end - start);
    next_deadline += kConfig.period ();
  }

  return samples;
}

template <typename Logger>
auto run_benchmark (Logger& logger, std::size_t thread_count)
    -> std::vector<std::chrono::nanoseconds> {
  std::vector<std::vector<std::chrono::nanoseconds>> thread_samples (
      thread_count);

  std::barrier start_barrier {static_cast<std::ptrdiff_t> (thread_count)};

  {
    std::vector<std::jthread> threads;
    threads.reserve (thread_count);

    for (const auto thread_id : std::views::iota (0uz, thread_count)) {
      const auto cpu = kConfig.producer_cpu_base + thread_id;

      threads.emplace_back ([&logger,
                             thread_sample = &thread_samples[thread_id],
                             &start_barrier, cpu] {
        *thread_sample = run_producer (logger, cpu, start_barrier);
      });
    }
  }

  return thread_samples | std::views::join | std::ranges::to<std::vector> ();
}

template <typename Logger, typename... LoggerArgs>
void run_case (std::ofstream& output_file,
               std::string_view type,
               std::size_t thread_count,
               LoggerArgs&&... logger_args) {
  const auto log_file = std::format ("bench_{}_{}.log", type, thread_count);

  auto logger = std::make_unique<Logger> (
      log_file, std::forward<LoggerArgs> (logger_args)...);

  auto samples = run_benchmark (*logger, thread_count);

  std::optional<std::size_t> dropped_messages;

  if constexpr (requires { logger->dropped_count (); }) {
    dropped_messages = logger->dropped_count ();
  }

  const auto stats =
      alll::bench::LatencyStats::from_samples (samples, dropped_messages);

  output_file << stats.print (type, thread_count);
  std::filesystem::remove (log_file);
}

}  // namespace

int main () {
  std::string_view result_file {"bench_results.csv"};
  std::ofstream csv (result_file.data ());
  csv << alll::bench::kCsvHeader;

  const auto run = [&csv]<typename Logger, typename... LoggerArgs> (
                       std::string_view type, std::size_t thread_count,
                       LoggerArgs&&... logger_args) {
    run_case<Logger> (csv, type, thread_count,
                      std::forward<LoggerArgs> (logger_args)...);
  };

  alll::Affinity async_consumer_thread {kConfig.consumer_cpu};

  for (const auto threads : kConfig.thread_counts) {
    std::println ("-- threads={} --", threads);
    run.operator()<alll::SyncLogger> ("sync", threads);
    run.operator()<alll::AsyncLogger> ("async", threads, async_consumer_thread);
  }

  std::println ("-- threads={} (OVERLOAD: exceeds physical core count) --",
                kConfig.overload_thread_count);
  run.operator()<alll::SyncLogger> ("sync_overload",
                                    kConfig.overload_thread_count);
  run.operator()<alll::AsyncLogger> (
      "async_overload", kConfig.overload_thread_count, async_consumer_thread);

  std::println ("\nRESULTS written to [{}]\n",
                std::format ("\033[32m{}\033[0m", result_file));
  return 0;
}
