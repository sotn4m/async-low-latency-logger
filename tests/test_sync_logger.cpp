#include "logger/sync_logger.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(SyncLoggerTest, SingleMessageIsWrittenWithLevelAndText) {
  const std::string path = "alll_test_sync_single.log";
  std::remove(path.c_str());

  {
    alll::SyncLogger logger(path);
    logger.log(alll::LogLevel::Warn, "hello world");
  }

  std::ifstream in(path);
  std::string line;
  ASSERT_TRUE(std::getline(in, line));
  EXPECT_NE(line.find("Warn"), std::string::npos);
  EXPECT_NE(line.find("hello world"), std::string::npos);
}

// The mutex is what's under test here: many threads writing concurrently
// must never interleave/tear a line. If it did, the line count wouldn't
// match, or a getline would produce a garbled fragment.
TEST(SyncLoggerTest, ConcurrentWritesAreNeverTornOrLost) {
  const std::string path = "alll_test_sync_concurrent.log";
  std::remove(path.c_str());

  constexpr int kThreads = 8;
  constexpr int kPerThread = 2'000;

  {
    alll::SyncLogger logger(path);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&logger, t] {
        for (int i = 0; i < kPerThread; ++i) {
          logger.log(alll::LogLevel::Info, "thread {} msg {}", t, i);
        }
      });
    }
    for (auto& th : threads) th.join();
  }

  std::ifstream in(path);
  std::size_t lines = 0;
  std::string line;
  while (std::getline(in, line)) {
    EXPECT_NE(line.find("thread "), std::string::npos) << "torn/garbled line: " << line;
    ++lines;
  }
  EXPECT_EQ(lines, static_cast<std::size_t>(kThreads) * kPerThread);
}

} // namespace
