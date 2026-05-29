#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sequitur/core/IngressPacket.hpp>

namespace sequitur {
namespace concurrency {

class MPSCQueue {
private:
  static constexpr size_t Capacity = 2048; // Must be a power of 2
  static constexpr size_t Mask = Capacity - 1;

  struct Cell {
    std::atomic<size_t> sequence;
    core::IngressPacket data;
  };

  // Flat, contiguous memory block. No heap pointers or vector indirection.
  Cell buffer_[Capacity];

  // Isolate cursors onto separate 64-byte cache lines to stop L1 cache bouncing
  alignas(64) std::atomic<size_t> tail_{
      0}; // Contended on by multiple Ingress producers
  alignas(64) std::atomic<size_t> head_{
      0}; // Strictly read/written by the single Consumer core

  std::atomic<bool> stop_flag_{false};

public:
  MPSCQueue() {
    for (size_t i = 0; i < Capacity; ++i) {
      buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  ~MPSCQueue() = default;

  // Multi-Producer Ingress Path (Concurrent, Lock-Free CAS)
  bool push(core::IngressPacket packet) {
    if (stop_flag_.load(std::memory_order_relaxed)) [[unlikely]] {
      return false;
    }

    size_t t = tail_.load(std::memory_order_relaxed);

    while (true) {
      Cell &cell = buffer_[t & Mask];
      size_t seq = cell.sequence.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(t);

      if (diff == 0) {
        // Contention check: Atomically claim the ticket slot
        if (tail_.compare_exchange_weak(t, t + 1, std::memory_order_relaxed)) {
          cell.data = packet; // Efficient plain-old-data copy

          // Pass transaction ownership over to the matching core
          cell.sequence.store(t + 1, std::memory_order_release);
          return true;
        }
      } else if (diff < 0) {
        // Queue is completely full (Trigger backpressure upstream)
        return false;
      } else {
        // Lost the CAS race to another network thread. Reload tail and try
        // again.
        t = tail_.load(std::memory_order_relaxed);
      }
    }
  }

  // Single-Consumer Hot Path (O(1) Pure Linear Extraction - Zero CAS Overhead)
  std::optional<core::IngressPacket> pop() {
    size_t h = head_.load(std::memory_order_relaxed);

    Cell &cell = buffer_[h & Mask];
    size_t seq = cell.sequence.load(std::memory_order_acquire);
    intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(h + 1);

    if (diff == 0) {
      // Safe from races: We are guaranteed to be the only thread executing
      // pop()
      head_.store(h + 1, std::memory_order_relaxed);
      core::IngressPacket packet = cell.data;

      // Reset the generation ticket to cycle ownership back to the producers
      cell.sequence.store(h + Capacity, std::memory_order_release);
      return packet;
    }

    // Queue is empty
    return std::nullopt;
  }

  void shutdown() noexcept {
    stop_flag_.store(true, std::memory_order_relaxed);
  }

  bool empty() const noexcept {
    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_relaxed);
    return t == h;
  }
};

} // namespace concurrency
} // namespace sequitur
