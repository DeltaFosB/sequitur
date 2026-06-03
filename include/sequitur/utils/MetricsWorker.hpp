#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sequitur/concurrency/SPSCQueue.hpp>
#include <sequitur/core/MetricsPacket.hpp>
#include <thread>

namespace sequitur {
namespace utils {

class MetricsWorker {
private:
  static constexpr std::size_t NUM_BUCKETS = 2048;
  static constexpr uint64_t CPU_HZ = 3600000000;

  // The worker holds a shared pointer to the queue instantiated in main
  std::shared_ptr<concurrency::SPSCQueue<core::MetricsPacket, 4096>> queue_;

  std::atomic<bool> active_{false};
  std::thread worker_thread_;

  // Thread-local metrics structures populated entirely within Core 4's context
  uint32_t latency_buckets_[NUM_BUCKETS]{};
  uint64_t latency_outliers_{0};
  uint64_t backpressure_events_{0};

  void logging_loop();

public:
  explicit MetricsWorker(
      std::shared_ptr<concurrency::SPSCQueue<core::MetricsPacket, 4096>> queue);
  ~MetricsWorker();

  void shutdown() noexcept;
};

} // namespace utils
} // namespace sequitur
