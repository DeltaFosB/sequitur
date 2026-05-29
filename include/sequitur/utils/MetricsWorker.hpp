#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <sequitur/core/MatchingEngine.hpp>
#include <thread>

namespace sequitur {
namespace utils {

class MetricsWorker {
private:
  static constexpr std::size_t NUM_BUCKETS = 2048;
  static constexpr uint64_t CPU_HZ = 3600000000;

  const core::MatchingEngine &engine_;
  std::atomic<bool> active_{false};
  std::thread worker_thread_;

  alignas(64) std::atomic<uint32_t> latency_buckets_[NUM_BUCKETS]{};
  std::atomic<uint64_t> latency_outliers_{0};

  std::atomic<uint64_t> backpressure_events_{0};

  void logging_loop();

public:
  MetricsWorker(const core::MatchingEngine &engine);
  ~MetricsWorker();

  void record_latency(uint64_t delta_cycles) noexcept {
    uint64_t latency_ns = (delta_cycles * 1000000000) / CPU_HZ;

    if (latency_ns < NUM_BUCKETS) {
      latency_buckets_[latency_ns].fetch_add(1, std::memory_order_relaxed);
    } else {
      latency_outliers_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void record_backpressure() noexcept {
    backpressure_events_.fetch_add(1, std::memory_order_relaxed);
  }
};

} // namespace utils
} // namespace sequitur
