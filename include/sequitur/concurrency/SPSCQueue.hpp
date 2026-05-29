#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sequitur/core/EgressPacket.hpp>

namespace sequitur {
namespace concurrency {

class SPSCQueue {
private:
  static constexpr size_t Capacity = 2048; // Must be a power of 2
  static constexpr size_t Mask = Capacity - 1;

  // Flat, raw array built directly into the class memory block. Zero heap
  // allocations.
  core::EgressPacket buffer_[Capacity];

  // Extreme Cache Isolation: Separate cursors onto their own cache lines
  // to guarantee 0% false sharing between the core and the network thread.
  alignas(64) std::atomic<size_t> tail_{
      0}; // Modified ONLY by the Core Producer
  alignas(64) std::atomic<size_t> head_{
      0}; // Modified ONLY by the Outbound Consumer

  std::atomic<bool> stop_flag_{false};

public:
  SPSCQueue() = default;
  ~SPSCQueue() = default;

  // Core Hot Path Egress Push (Zero CAS - Pure Linear Increment)
  bool push(core::EgressPacket packet) {
    if (stop_flag_.load(std::memory_order_relaxed)) [[unlikely]] {
      return false;
    }

    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_acquire); // Acquire consumer state

    if (((t + 1) & Mask) == (h & Mask)) {
      // Queue is completely full (Outbound buffer backpressure)
      return false;
    }

    buffer_[t & Mask] = packet; // Copy pod structure directly into slot
    tail_.store(
        t + 1,
        std::memory_order_release); // Release visibility to network thread
    return true;
  }

  // Outbound Network Thread Extraction (Zero CAS - Pure Linear Read)
  std::optional<core::EgressPacket> pop() {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t t = tail_.load(std::memory_order_acquire); // Acquire producer state

    if (h == t) {
      // Queue is empty
      return std::nullopt;
    }

    core::EgressPacket packet = buffer_[h & Mask];
    head_.store(
        h + 1,
        std::memory_order_release); // Release slot back to core for reuse
    return packet;
  }

  bool empty() const noexcept {
    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_relaxed);
    return t == h;
  }

  void shutdown() noexcept {
    stop_flag_.store(true, std::memory_order_relaxed);
  }
};

} // namespace concurrency
} // namespace sequitur
