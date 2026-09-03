#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <vector>

namespace alll::bench {

inline constexpr std::string_view kCsvHeader =
    "logger,threads,n,p50_us,p90_us,p99_us,p99_9_us,p99_99_us,max_us,dropped,"
    "dropped_rate\n";

struct LatencyStats {
  std::size_t count {0uz};
  std::chrono::nanoseconds p50 {0};
  std::chrono::nanoseconds p90 {0};
  std::chrono::nanoseconds p99 {0};
  std::chrono::nanoseconds p999 {0};
  std::chrono::nanoseconds p9999 {0};
  std::chrono::nanoseconds max {0};

  std::optional<std::size_t> async_dropped_messages {};
  static auto from_samples (std::vector<std::chrono::nanoseconds>& samples,
                            std::optional<std::size_t> dropped) -> LatencyStats;

  [[nodiscard]] auto print (std::string_view label,
                            std::size_t thread_count) const -> std::string;
};

inline auto LatencyStats::from_samples (
    std::vector<std::chrono::nanoseconds>& samples,
    std::optional<std::size_t> dropped) -> LatencyStats {
  LatencyStats stats {};
  if (samples.empty ()) {
    return stats;
  }

  std::ranges::sort (samples);
  stats.count = samples.size ();

  const auto at_percentile = [&samples] (double p) {
    const auto idx = static_cast<std::size_t> (
        p * static_cast<double> (samples.size () - 1));
    return samples[idx];
  };

  stats.p50 = at_percentile (0.50);
  stats.p90 = at_percentile (0.90);
  stats.p99 = at_percentile (0.99);
  stats.p999 = at_percentile (0.999);
  stats.p9999 = at_percentile (0.9999);
  stats.max = samples.back ();
  stats.async_dropped_messages = dropped;
  return stats;
}

inline auto LatencyStats::print (std::string_view label,
                                 std::size_t thread_count) const
    -> std::string {
  const auto us = [] (std::chrono::nanoseconds d) {
    return std::chrono::duration<double, std::micro> {d}.count ();
  };

  std::string dropped_col {};
  std::string dropped_ratio_col {};
  if (async_dropped_messages) {
    const auto dropped = *async_dropped_messages;
    const auto ratio =
        100.0 * static_cast<double> (dropped) / static_cast<double> (count);
    dropped_col = std::format ("{}", dropped);
    dropped_ratio_col = std::format ("{:.4f}", ratio);
  }

  return std::format (
      "{},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{},{}\n", label,
      thread_count, count, us (p50), us (p90), us (p99), us (p999), us (p9999),
      us (max), dropped_col, dropped_ratio_col);
}

}  // namespace alll::bench
