#include "logger/async_logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <gtest/gtest.h>

namespace {

// AsyncLogger's destructor (via ~jthread) calls request_stop() then join()
// on the consumer thread. If the consumer is idle (buffer empty, sitting
// in its back-off path) at that moment and never re-checks the stop
// token there, request_stop() is invisible to it and join() blocks
// forever — every AsyncLogger in the program would hang on destruction
// as soon as it goes idle before being destroyed, which is the common
// case, not an edge case.
//
// A hung join() can't be caught with a plain EXPECT/ASSERT on the main
// test thread — the call itself never returns. So the logger is
// constructed and destroyed on a detached worker thread instead, and
// this test polls a completion flag with a bounded deadline. If the
// worker is still stuck when the deadline passes, we report failure and
// move on; the leaked, permanently-blocked worker thread doesn't stop
// the test binary from exiting, since process exit tears down all
// threads regardless of join state.
TEST(AsyncLoggerTest, DestructorDoesNotHangWhenIdleBeforeShutdown) {
  const std::string path = "alll_test_idle_shutdown.log";
  std::remove(path.c_str());

  std::atomic<bool> completed{false};
  std::thread worker([&] {
    {
      alll::AsyncLogger logger(path);
      logger.log(alll::LogLevel::Info, "just one message");
      // Give the consumer thread time to fully drain and settle into
      // its empty-buffer back-off path before we destroy the logger.
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } // ~AsyncLogger runs here — must return, not hang.
    // release: publishes everything the worker touched (including its
    // read of `path`) so the main thread's acquiring load below is
    // guaranteed to happen-after it, not just usually-observes-it-in-time.
    completed.store(true, std::memory_order_release);
  });
  worker.detach();

  constexpr auto kTimeout = std::chrono::seconds(3);
  const auto deadline = std::chrono::steady_clock::now() + kTimeout;
  while (!completed.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_TRUE(completed.load(std::memory_order_acquire))
      << "AsyncLogger destructor appears to have hung: the consumer "
         "thread likely never re-checks stop_token while idle.";
}

} // namespace
