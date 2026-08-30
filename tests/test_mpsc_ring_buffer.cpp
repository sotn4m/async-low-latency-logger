#include "logger/mpsc_ring_buffer.hpp"

#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(MpscRingBufferTest, EdgeCasesFullAndMultiLapWraparound) {
  alll::MpscRingBuffer<int, 4> rb;

  for (int i = 1; i <= 4; ++i) {
    EXPECT_TRUE(rb.try_push(i));
  }
  EXPECT_FALSE(rb.try_push(5)) << "must reject, not overwrite or block, when full";

  for (int expected = 1; expected <= 4; ++expected) {
    auto v = rb.try_pop();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, expected);
  }
  EXPECT_FALSE(rb.try_pop());

  for (int lap = 0; lap < 3; ++lap) {
    for (int i = 0; i < 4; ++i) {
      EXPECT_TRUE(rb.try_push(lap * 4 + i));
    }
    for (int i = 0; i < 4; ++i) {
      auto v = rb.try_pop();
      ASSERT_TRUE(v);
      EXPECT_EQ(*v, lap * 4 + i);
    }
  }
}

// Encodes (producer_id, sequence_within_producer) so the consumer can
// check per-producer ordering — global ordering across producers is
// inherently unspecified for MPSC, only each producer's own stream needs
// to stay in order. Small capacity + several producers means heavy,
// constant contention on the CAS claim loop in try_push. Build with
// -DALLL_ENABLE_TSAN=ON to verify there's no race, not just right values.
TEST(MpscRingBufferTest, ConcurrentProducersPreservePerProducerOrder) {
  constexpr int kProducers = 4;
  constexpr std::size_t kCapacity = 16;
  constexpr std::uint64_t kPerProducer = 200'000;
  constexpr std::uint64_t kTotal = kProducers * kPerProducer;

  auto encode = [](int producer, std::uint64_t seq) -> std::uint64_t {
    return (static_cast<std::uint64_t>(producer) << 48) | seq;
  };

  alll::MpscRingBuffer<std::uint64_t, kCapacity> rb;
  std::vector<std::uint64_t> last_seen(kProducers, 0);

  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (std::uint64_t s = 1; s <= kPerProducer; ++s) {
        while (!rb.try_push(encode(p, s))) {
          // full — never block per design, just retry
        }
      }
    });
  }

  std::thread consumer([&] {
    std::uint64_t got = 0;
    while (got < kTotal) {
      auto v = rb.try_pop();
      if (!v) continue;
      const int p = static_cast<int>(*v >> 48);
      const std::uint64_t s = *v & 0xFFFFFFFFFFFFULL;
      ASSERT_EQ(s, last_seen[p] + 1) << "order violation for producer " << p;
      last_seen[p] = s;
      ++got;
    }
  });

  for (auto& t : producers) t.join();
  consumer.join();

  for (int p = 0; p < kProducers; ++p) {
    EXPECT_EQ(last_seen[p], kPerProducer) << "producer " << p << " lost messages";
  }
}

} // namespace
