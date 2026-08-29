#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>

namespace alll {

inline constexpr std::size_t kCacheLineSize = 64;

template <std::size_t N>
concept PowerOfTwoSize = N > 1 && std::has_single_bit (N);

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
class SpscRingBuffer {
 public:
  SpscRingBuffer () = default;

  SpscRingBuffer (const SpscRingBuffer&) = delete;
  SpscRingBuffer& operator= (const SpscRingBuffer&) = delete;

  [[nodiscard]] auto try_push (const T& value) -> bool;
  [[nodiscard]] bool try_push (T&& item);
  [[nodiscard]] std::optional<T> try_pop ();

  [[nodiscard]] std::size_t capacity () const noexcept { return Capacity; }
  [[nodiscard]] std::size_t size () const noexcept;
  [[nodiscard]] bool empty () const noexcept;
  [[nodiscard]] bool full () const noexcept;

 private:
  static constexpr std::size_t kMask = Capacity - 1;
  std::array<T, Capacity> buffer_;
  alignas (kCacheLineSize) std::atomic<std::size_t> write_index_ {0};
  alignas (kCacheLineSize) std::atomic<std::size_t> read_index_ {0};
};

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
auto SpscRingBuffer<T, Capacity>::try_push (const T& item) -> bool {
  auto write = write_index_.load (std::memory_order_relaxed);
  auto read = read_index_.load (std::memory_order_acquire);

  if ((write - read) == capacity ())
    return false;

  auto write_index = write & (kMask);
  buffer_[write_index] = item;
  write_index_.store (++write, std::memory_order_release);
  return true;
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
auto SpscRingBuffer<T, Capacity>::try_push (T&& item) -> bool {
  auto write = write_index_.load (std::memory_order_relaxed);
  auto read = read_index_.load (std::memory_order_acquire);

  if ((write - read) == capacity ())
    return false;

  auto write_index = write & (kMask);
  buffer_[write_index] = std::move (item);
  write_index_.store (++write, std::memory_order_release);
  return true;
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
auto SpscRingBuffer<T, Capacity>::try_pop () -> std::optional<T> {
  auto read = read_index_.load (std::memory_order_relaxed);
  auto write = write_index_.load (std::memory_order_acquire);

  if (read == write)
    return std::nullopt;

  auto read_index = read & (kMask);
  T elem = std::move (buffer_[read_index]);
  read_index_.store (++read, std::memory_order_release);
  return elem;
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
auto SpscRingBuffer<T, Capacity>::size () const noexcept -> std::size_t {
  auto write = write_index_.load (std::memory_order_acquire);
  auto read = read_index_.load (std::memory_order_acquire);
  return write - read;
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
bool SpscRingBuffer<T, Capacity>::full () const noexcept {
  // Full is called by the producer
  auto write = write_index_.load (std::memory_order_relaxed);
  auto read = read_index_.load (std::memory_order_acquire);
  return (write - read) == Capacity;
}

template <typename T, std::size_t Capacity>
  requires PowerOfTwoSize<Capacity>
bool SpscRingBuffer<T, Capacity>::empty () const noexcept {
  // Called by the consumer; load on read_ can mem_ordr_relaxed
  auto read = read_index_.load (std::memory_order_relaxed);
  // Producer modifies write_; we need mem_ordr_acquire
  auto write = write_index_.load (std::memory_order_acquire);
  return write == read;
}

}  // namespace alll
