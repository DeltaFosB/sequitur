#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sequitur/utils/MetricsWorker.hpp>

namespace sequitur {
namespace utils {

MetricsWorker::MetricsWorker(
    std::shared_ptr<concurrency::SPSCQueue<core::MetricsPacket, 4096>> queue)
    : queue_(std::move(queue)), active_(true),
      worker_thread_(&MetricsWorker::logging_loop, this) {}

MetricsWorker::~MetricsWorker() { shutdown(); }

void MetricsWorker::shutdown() noexcept {
  if (active_.load(std::memory_order_relaxed)) {
    active_.store(false, std::memory_order_relaxed);
    if (queue_) {
      queue_->shutdown();
    }
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
  }
}

void MetricsWorker::logging_loop() {
  uint64_t last_orders = 0;
  core::MetricsPacket packet;

  uint64_t current_orders = 0;
  uint64_t current_trades = 0;
  uint64_t pool_used = 0;
  uint64_t pool_failures = 0;
  uint64_t pool_peak = 0;

  auto last_report_time = std::chrono::steady_clock::now();
  const auto report_interval = std::chrono::milliseconds(250);

  while (active_.load(std::memory_order_relaxed)) {
    bool data_processed = false;

    // Drain the lock-free queue instantly as data arrives without an initial
    // sleep block
    while (queue_ && queue_->pop(packet)) {
      data_processed = true;

      if (packet.type == core::MetricsUpdateType::BACKPRESSURE_DETECTED) {
        backpressure_events_++;
      }

      current_orders = packet.total_orders;
      current_trades = packet.total_trades;
      pool_used = packet.pool_used;
      pool_failures = packet.pool_failures;
      pool_peak = packet.pool_peak;

      uint64_t latency_ns = (packet.latency_cycles * 1000000000) / CPU_HZ;
      if (latency_ns < NUM_BUCKETS) {
        latency_buckets_[latency_ns]++;
      } else {
        latency_outliers_++;
      }
    }

    // If the queue was empty, drop into a micro-sleep here to free up core
    // cycles
    if (!data_processed) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    uint64_t delta_orders = current_orders - last_orders;
    uint64_t throughput_ops = delta_orders * 10;
    last_orders = current_orders;

    double fill_rate = (current_orders > 0)
                           ? static_cast<double>(current_trades) /
                                 static_cast<double>(current_orders)
                           : 0.0;

    double pool_hit_rate =
        (current_orders > 0)
            ? static_cast<double>(current_orders - pool_failures) /
                  static_cast<double>(current_orders)
            : 1.0;

    uint64_t total_samples = latency_outliers_;
    for (size_t i = 0; i < NUM_BUCKETS; ++i) {
      total_samples += latency_buckets_[i];
    }

    uint64_t p50_ns = 0, p99_ns = 0, p999_ns = 0;

    if (total_samples > 0) {
      uint64_t running_sum = 0;
      bool hit_p50 = false, hit_p99 = false, hit_p999 = false;

      uint64_t target_p50 = total_samples * 0.50;
      uint64_t target_p99 = total_samples * 0.99;
      uint64_t target_p999 = total_samples * 0.999;

      for (size_t i = 0; i < NUM_BUCKETS; ++i) {
        running_sum += latency_buckets_[i];

        if (!hit_p50 && running_sum >= target_p50) {
          p50_ns = i;
          hit_p50 = true;
        }
        if (!hit_p99 && running_sum >= target_p99) {
          p99_ns = i;
          hit_p99 = true;
        }
        if (!hit_p999 && running_sum >= target_p999) {
          p999_ns = i;
          hit_p999 = true;
          break;
        }
      }

      if (!hit_p50)
        p50_ns = NUM_BUCKETS - 1;
      if (!hit_p99)
        p99_ns = NUM_BUCKETS - 1;
      if (!hit_p999)
        p999_ns = NUM_BUCKETS - 1;
    }

    char buffer[1024];
    char *ptr = buffer;
    char *end = buffer + sizeof(buffer);

    auto append_str = [&ptr, end](const char *str, size_t len) {
      if (ptr + len < end) {
        std::memcpy(ptr, str, len);
        ptr += len;
      }
    };

    auto current_time = std::chrono::steady_clock::now();
    if (current_time - last_report_time >= report_interval) {
      last_report_time = current_time;
      append_str("{", 1);
      append_str("\"latency_scope\":\"matching_core_isolated\",", 41);
      append_str("\"clock_source\":\"rdtsc\",", 23);
      append_str("\"cpu_hz_estimated\":3600000000,", 30);

      append_str("\"total_orders\":", 15);
      ptr = std::to_chars(ptr, end, current_orders).ptr;
      append_str(",", 1);

      append_str("\"total_trades\":", 15);
      ptr = std::to_chars(ptr, end, current_trades).ptr;
      append_str(",", 1);

      append_str("\"fill_rate\":", 12);
      ptr = std::to_chars(ptr, end, fill_rate, std::chars_format::fixed, 4).ptr;
      append_str(",", 1);

      append_str("\"throughput_ops\":", 17);
      ptr = std::to_chars(ptr, end, throughput_ops).ptr;
      append_str(",", 1);

      append_str("\"p50_latency_ns\":", 17);
      ptr = std::to_chars(ptr, end, p50_ns).ptr;
      append_str(",", 1);

      append_str("\"p99_latency_ns\":", 17);
      ptr = std::to_chars(ptr, end, p99_ns).ptr;
      append_str(",", 1);

      append_str("\"p999_latency_ns\":", 18);
      ptr = std::to_chars(ptr, end, p999_ns).ptr;
      append_str(",", 1);

      append_str("\"pool_used_objects\":", 20);
      ptr = std::to_chars(ptr, end, pool_used).ptr;
      append_str(",", 1);

      append_str("\"pool_peak_objects\":", 20);
      ptr = std::to_chars(ptr, end, pool_peak).ptr;
      append_str(",", 1);

      append_str("\"memory_pool_hit_rate\":", 23);
      ptr = std::to_chars(ptr, end, pool_hit_rate, std::chars_format::fixed, 4)
                .ptr;
      append_str(",", 1);

      append_str("\"ring_buffer_backpressure_events\":", 34);
      ptr = std::to_chars(ptr, end, backpressure_events_).ptr;

      append_str("}\n", 2);

      std::size_t message_len = ptr - buffer;
      std::cout.write(buffer, message_len);

      std::cout.flush();
    }
  }
}

} // namespace utils
} // namespace sequitur
