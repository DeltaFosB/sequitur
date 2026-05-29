#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace sequitur {
namespace memory {

template <typename T> class ObjectPool {
private:
  T *pool;
  uint32_t *free_indices;
  int64_t top;
  size_t capacity;

  // Observability
  std::atomic<size_t> used_count{0};
  std::atomic<size_t> alloc_failures{0};
  std::atomic<size_t> peak_usage{0};

public:
  ObjectPool(size_t cap) : capacity(cap) {
    size_t total_size = sizeof(T) * cap;
    pool = static_cast<T *>(std::aligned_alloc(64, total_size));
    free_indices = new uint32_t[cap];
    for (size_t i = 0; i < cap; i++) {
      free_indices[i] = i;
    }

    top = cap - 1;
  }

  T *acquire() {
    if (top < 0) {
      alloc_failures.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }

    size_t old_count = used_count.fetch_add(1, std::memory_order_relaxed);

    size_t obs_used_count = old_count + 1;
    size_t obs_peak_usage = peak_usage.load(std::memory_order_relaxed);

    while (obs_used_count > obs_peak_usage)
      if (peak_usage.compare_exchange_weak(obs_peak_usage, obs_used_count,
                                           std::memory_order_relaxed))
        break;

    uint32_t idx = free_indices[top--];

    return new (pool + idx) T();
  }

  void release(T *ptr) {
    if (ptr != nullptr) {
      used_count.fetch_sub(1, std::memory_order_relaxed);
      ptr->~T();
      uint32_t idx = ptr - pool;
      free_indices[++top] = idx;
    }
  }
  size_t get_used_count() const {
    return used_count.load(std::memory_order_relaxed);
  }

  size_t get_alloc_failures() const {
    return alloc_failures.load(std::memory_order_relaxed);
  }

  size_t get_peak_usage() const {
    return peak_usage.load(std::memory_order_relaxed);
  }

  size_t get_capacity() const { return capacity; }

  ~ObjectPool() {
    std::free(pool);
    delete[] free_indices;
  }
};
} // namespace memory
} // namespace sequitur
