#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <iterator>
#include <optional>

namespace alll {

inline constexpr std::size_t kCacheLineSize = 64;

template <std::size_t N>
concept PowerOfTwoSize = N > 1 && std::has_single_bit (N);

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
class MpscRingBuffer {
 public:
  MpscRingBuffer ();

  MpscRingBuffer (const MpscRingBuffer&) = delete;
  MpscRingBuffer& operator= (const MpscRingBuffer&) = delete;

  [[nodiscard]] bool try_push (const T& value);
  [[nodiscard]] bool try_push (T&& value);

  [[nodiscard]] std::optional<T> try_pop ();

 private:
  static constexpr std::size_t kMask = Capacity - 1;

  struct Slot {
    T data;
    std::atomic<std::size_t> sequence;
  };

  std::array<Slot, Capacity> slots_;

  alignas (kCacheLineSize) std::atomic<std::size_t> write_ {0};
  alignas (kCacheLineSize) std::atomic<std::size_t> read_ {0};
};

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
MpscRingBuffer<T, Capacity>::MpscRingBuffer () {
  for (auto i {0uz}; i < Capacity; ++i) {
    slots_[i].sequence = i;
  }
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
auto MpscRingBuffer<T, Capacity>::try_push (const T& value) -> bool {
  while (true) {
    auto pos = write_.load (std::memory_order_relaxed);
    auto sequence =
        slots_[pos & kMask].sequence.load (std::memory_order_acquire);

    auto diff = static_cast<std::ptrdiff_t> (sequence) -
                static_cast<std::ptrdiff_t> (pos);

    if (diff < 0) {
      // ring buffer is full
      return false;
    }

    if (diff > 0) {
      // slot already claimed
      continue;
    }

    if (write_.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
      slots_[pos & kMask].data = value;
      slots_[pos & kMask].sequence.store (pos + 1, std::memory_order_release);
      break;
    }
  }
  return true;
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
auto MpscRingBuffer<T, Capacity>::try_push (T&& value) -> bool {
  while (true) {
    auto pos = write_.load (std::memory_order_relaxed);
    auto sequence =
        slots_[pos & kMask].sequence.load (std::memory_order_acquire);

    auto diff = static_cast<std::ptrdiff_t> (sequence) -
                static_cast<std::ptrdiff_t> (pos);

    if (diff < 0) {
      // ring buffer is full
      return false;
    }

    if (diff > 0) {
      // slot already claimed
      continue;
    }

    if (write_.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
      slots_[pos & kMask].data = std::move (value);
      slots_[pos & kMask].sequence.store (pos + 1, std::memory_order_release);
      break;
    }
  }
  return true;
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
auto MpscRingBuffer<T, Capacity>::try_pop () -> std::optional<T> {
  auto pos = read_.load (std::memory_order_relaxed);
  auto sequence = slots_[pos & kMask].sequence.load (std::memory_order_acquire);

  if (sequence <= pos) {
    // if sequnce is less or equal
    // than pos there is nothing to consume
    return std::nullopt;
  }

  auto result = slots_[pos & kMask].data;
  slots_[pos & kMask].sequence.store (pos + Capacity,
                                      std::memory_order_release);
  read_.store (pos + 1, std::memory_order_relaxed);

  return result;
}

}  // namespace alll
