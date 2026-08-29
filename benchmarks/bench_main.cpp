#include <cstdio>

// Milestone 1: benchmark harness skeleton.
//
// TODO(milestone-1):
//   - Spin up N producer threads calling SyncLogger::log() in a loop.
//   - Use an *open-loop* load generator (schedule each call for a fixed
//     wall-clock offset from the start, e.g. via a Poisson or fixed-rate
//     schedule) rather than calling back-to-back — closed-loop generators
//     cause coordinated omission and hide tail latency.
//   - Time each call individually with std::chrono::steady_clock
//     (before/after the log() call only — that's the hot path being
//     measured), store per-thread, then merge and report p50/p90/p99/
//     p99.9/p99.99/max.
//   - Exclude a warm-up period from the reported stats.
//   - Once the async ring-buffer logger exists (later milestones), add
//     it here as a second target so both can be compared side by side
//     under identical load.
int main() {
  std::puts("bench_logger: TODO - implement milestone 1 harness");
  return 0;
}
