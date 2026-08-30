#include "logger/spsc_ring_buffer.hpp"

#include <cstddef>
#include <thread>

#include <gtest/gtest.h>

namespace {

TEST(SpscRingBufferTest, EdgeCasesFullEmptyAndWraparound) {
  alll::SpscRingBuffer<int, 4> rb;
  EXPECT_TRUE(rb.empty());
  EXPECT_FALSE(rb.full());
  EXPECT_EQ(rb.size(), 0u);

  EXPECT_TRUE(rb.try_push(1));
  EXPECT_TRUE(rb.try_push(2));
  EXPECT_TRUE(rb.try_push(3));
  EXPECT_TRUE(rb.try_push(4));
  EXPECT_TRUE(rb.full());
  EXPECT_FALSE(rb.try_push(5)) << "must reject, not overwrite or block, when full";

  auto v = rb.try_pop();
  ASSERT_TRUE(v);
  EXPECT_EQ(*v, 1);
  EXPECT_FALSE(rb.full());

  EXPECT_TRUE(rb.try_push(5)); // wraps around now

  int expected = 2;
  while (auto x = rb.try_pop()) {
    EXPECT_EQ(*x, expected);
    ++expected;
  }
  EXPECT_EQ(expected, 6);
  EXPECT_TRUE(rb.empty());
}

// Small capacity forces many wraparounds over a modest item count — the
// point isn't raw throughput here, it's exercising the release/acquire
// handoff in both directions (producer publishing a slot, consumer
// freeing one) many times under real thread interleaving. Run this
// binary built with -DALLL_ENABLE_TSAN=ON to actually verify there's no
// data race, not just that the values come out right.
TEST(SpscRingBufferTest, ConcurrentProducerConsumerPreservesOrder) {
  constexpr std::size_t kCapacity = 16;
  constexpr std::size_t kN = 500'000;

  alll::SpscRingBuffer<std::size_t, kCapacity> rb;

  std::thread producer([&] {
    for (std::size_t i = 0; i < kN; ++i) {
      while (!rb.try_push(i)) {
        // full — never block per design, just retry
      }
    }
  });

  std::thread consumer([&] {
    std::size_t expected = 0;
    while (expected < kN) {
      auto v = rb.try_pop();
      if (!v) continue;
      ASSERT_EQ(*v, expected);
      ++expected;
    }
  });

  producer.join();
  consumer.join();

  EXPECT_TRUE(rb.empty());
}

} // namespace
