#pragma once

#include <atomic>
#include <cstddef>

namespace sequitur {
namespace concurrency {

template <typename T, size_t Capacity = 2048> class SPSCQueue {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Queue capacity must be a power of 2");

private:
  static constexpr size_t Mask = Capacity - 1;

  // Continuous memory block sized perfectly at compile time to maintain cache
  // line consistency
  alignas(64) T buffer_[Capacity];

  // Hardware Cache-Line Isolation: Guarantees 0% false sharing between core and
  // background rings
  alignas(64) std::atomic<size_t> tail_{
      0}; // Exclusively modified by the Producer thread
  alignas(64) std::atomic<size_t> head_{
      0}; // Exclusively modified by the Consumer thread

  // Instance-bound local tracking shadow cursor. Isolated via explicit
  // alignment to protect the producer's hot path workspace from cross-core
  // cache invalidation.
  alignas(64) size_t cached_head_{0};

  std::atomic<bool> stop_flag_{false};

public:
  SPSCQueue() = default;
  ~SPSCQueue() = default;

  // Delete copy/move semantics to lock down instance stability inside arenas or
  // memory segments
  SPSCQueue(const SPSCQueue &) = delete;
  SPSCQueue &operator=(const SPSCQueue &) = delete;
  SPSCQueue(SPSCQueue &&) noexcept = delete;
  SPSCQueue &operator=(SPSCQueue &&) noexcept = delete;

  // Zero-CAS Producer Stream (Pure Linear Relaxed Store)
  bool push(const T &item) noexcept {
    if (stop_flag_.load(std::memory_order_relaxed)) [[unlikely]] {
      return false;
    }

    size_t t = tail_.load(std::memory_order_relaxed);

    // Check shadow tracking first. Avoids cross-core L1 data cache misses
    // unless full
    if ((t - cached_head_) >= Capacity) {
      cached_head_ =
          head_.load(std::memory_order_acquire); // Explicit hardware sync fence
      if ((t - cached_head_) >= Capacity) {
        return false; // Queue is genuinely full (Trigger backpressure)
      }
    }

    buffer_[t & Mask] = item; // Direct POD copy into pre-allocated memory slot
    tail_.store(t + 1,
                std::memory_order_release); // Push visibility boundary out
    return true;
  }

  // Zero-CAS Consumer Extraction via Pass-by-Reference out-parameter
  bool pop(T &out_item) noexcept {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t t = tail_.load(
        std::memory_order_acquire); // Synchronize up to current producer state

    if (h == t) {
      return false; // Ring buffer is currently empty
    }

    out_item = buffer_[h & Mask]; // Zero-copy parameter mapping
    head_.store(h + 1, std::memory_order_release); // Hand slot ownership back
                                                   // to matching thread
    return true;
  }

  bool empty() const noexcept {
    return tail_.load(std::memory_order_relaxed) ==
           head_.load(std::memory_order_relaxed);
  }

  void shutdown() noexcept {
    stop_flag_.store(true, std::memory_order_relaxed);
  }
};

} // namespace concurrency
} // namespace sequitur
