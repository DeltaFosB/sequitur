#include <charconv>
#include <cstring>
#include <iostream>
#include <sequitur/utils/MetricsWorker.hpp>

namespace sequitur {
namespace utils {

MetricsWorker::MetricsWorker(const core::MatchingEngine &engine)
    : engine_(engine), active_(true),
      worker_thread_(&MetricsWorker::logging_loop, this) {

  // Self-Registration: Temporarily strip constness from the engine reference
  // to bind this worker instance's memory address ('this') to the hot path
  // pointer.
  const_cast<core::MatchingEngine &>(engine_).register_metrics_worker(this);
}

MetricsWorker::~MetricsWorker() {
  active_.store(false, std::memory_order_relaxed);
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void MetricsWorker::logging_loop() {
  uint64_t last_orders = 0;

  while (active_.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Asynchronously snapshot live counter values from the engine core
    uint64_t current_orders = engine_.get_total_orders();
    uint64_t current_trades = engine_.get_total_trades();
    size_t pool_used = engine_.get_pool_used();
    size_t pool_failures = engine_.get_pool_failures();
    size_t pool_peak = engine_.get_pool_peak();

    // Calculate dynamic volume and efficiency metrics
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

    // ---------------------------------------------------------------------
    // Dynamic Telemetry Math: Linear Percentile Aggregation
    // ---------------------------------------------------------------------
    uint64_t total_samples = 0;
    uint32_t local_buckets[NUM_BUCKETS];

    // Atomically harvest and flush the shared buckets to minimize contention
    // windows
    for (size_t i = 0; i < NUM_BUCKETS; ++i) {
      local_buckets[i] =
          latency_buckets_[i].exchange(0, std::memory_order_relaxed);
      total_samples += local_buckets[i];
    }

    // Include outlier events that transcended our tracking boundaries
    uint64_t outliers =
        latency_outliers_.exchange(0, std::memory_order_relaxed);
    total_samples += outliers;

    // Collect accumulated ring-buffer backpressure steps
    uint64_t backpressure_events =
        backpressure_events_.load(std::memory_order_relaxed);

    uint64_t p50_ns = 0;
    uint64_t p99_ns = 0;
    uint64_t p999_ns = 0;

    if (total_samples > 0) {
      uint64_t running_sum = 0;
      bool hit_p50 = false;
      bool hit_p99 = false;
      bool hit_p999 = false;

      // Define exact item rank indexes required for target cutoffs
      uint64_t target_p50 = total_samples * 0.50;
      uint64_t target_p99 = total_samples * 0.99;
      uint64_t target_p999 = total_samples * 0.999;

      for (size_t i = 0; i < NUM_BUCKETS; ++i) {
        running_sum += local_buckets[i];

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
          break; // All target distribution metrics calculated, break early
        }
      }

      // Fallback: If heavy tail jitter landed completely inside the outliers
      // bucket, clamp the metrics safely to our maximum tracked index boundary.
      if (!hit_p50)
        p50_ns = NUM_BUCKETS - 1;
      if (!hit_p99)
        p99_ns = NUM_BUCKETS - 1;
      if (!hit_p999)
        p999_ns = NUM_BUCKETS - 1;
    }

    // ---------------------------------------------------------------------
    // Fast JSON Telemetry String Construction Sequence
    // ---------------------------------------------------------------------
    char buffer[1024];
    char *ptr = buffer;
    char *end = buffer + sizeof(buffer);

    auto append_str = [&ptr, end](const char *str, size_t len) {
      if (ptr + len < end) {
        std::memcpy(ptr, str, len);
        ptr += len;
      }
    };

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
    ptr =
        std::to_chars(ptr, end, pool_hit_rate, std::chars_format::fixed, 4).ptr;
    append_str(",", 1);

    append_str("\"ring_buffer_backpressure_events\":", 34);
    ptr = std::to_chars(ptr, end, backpressure_events).ptr;

    append_str("}\n", 2);

    std::size_t message_len = ptr - buffer;
    std::cout.write(buffer, message_len);
  }
}

} // namespace utils
} // namespace sequitur
