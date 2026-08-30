#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace alll::bench {

struct LatencyStats {
  std::size_t count = 0;
  std::chrono::nanoseconds p50{0};
  std::chrono::nanoseconds p90{0};
  std::chrono::nanoseconds p99{0};
  std::chrono::nanoseconds p999{0};
  std::chrono::nanoseconds p9999{0};
  std::chrono::nanoseconds max{0};

  // Sorts `samples` in place (callers that need the raw samples again
  // afterward should copy first) and computes percentiles from it.
  static LatencyStats from_samples(std::vector<std::chrono::nanoseconds>& samples);

  void print(const char* label) const;
};

inline LatencyStats LatencyStats::from_samples(std::vector<std::chrono::nanoseconds>& samples) {
  LatencyStats stats;
  if (samples.empty()) {
    return stats;
  }

  std::sort(samples.begin(), samples.end());
  stats.count = samples.size();

  const auto at_percentile = [&](double p) {
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(samples.size() - 1));
    return samples[idx];
  };

  stats.p50 = at_percentile(0.50);
  stats.p90 = at_percentile(0.90);
  stats.p99 = at_percentile(0.99);
  stats.p999 = at_percentile(0.999);
  stats.p9999 = at_percentile(0.9999);
  stats.max = samples.back();
  return stats;
}

inline void LatencyStats::print(const char* label) const {
  const auto us = [](std::chrono::nanoseconds d) {
    return static_cast<double>(d.count()) / 1000.0;
  };
  std::printf(
      "%-8s n=%-8zu p50=%9.2fus p90=%9.2fus p99=%9.2fus p99.9=%9.2fus p99.99=%9.2fus max=%9.2fus\n",
      label, count, us(p50), us(p90), us(p99), us(p999), us(p9999), us(max));
}

} // namespace alll::bench
