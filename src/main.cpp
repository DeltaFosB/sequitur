#include <chrono>
#include <iostream>
#include <memory>
#include <sequitur/concurrency/SPSCQueue.hpp>
#include <sequitur/core/MatchingEngine.hpp>
#include <sequitur/utils/MetricsWorker.hpp>
#include <thread>

// Simple inline assembly helper to read raw hardware clock cycles
inline uint64_t read_rdtsc() noexcept {
  unsigned int lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

int main() {
  using namespace sequitur;

  // Instantiate the shared queue using a shared_pointer context
  auto metrics_queue =
      std::make_shared<concurrency::SPSCQueue<core::MetricsPacket, 4096>>();

  // MetricsWorker initializes on its own background thread, holding the queue
  // reader end
  utils::MetricsWorker worker(metrics_queue);

  std::cout
      << "[System Initialized] Telemetry completely decoupled out-of-band."
      << std::endl;

  // Launch the Core Thread representing our single-threaded shared-nothing
  // matching loop
  std::thread core_thread([metrics_queue]() {
    core::MatchingEngine engine(1000000);

    // Simulated high-velocity trading ingress event loop
    for (size_t i = 0; i < 1'000'000; ++i) {
      uint64_t start_cycles = read_rdtsc();

      // 1. Execute matching logic at 100% pure assembly speed
      engine.submit_order(i % 2, 100, 10);

      uint64_t end_cycles = read_rdtsc();
      uint64_t elapsed_cycles = end_cycles - start_cycles;

      // 2. Extract hot memory pool metrics directly from the local core state
      size_t pool_used = engine.get_pool_used();
      size_t pool_failures = engine.get_pool_failures();
      size_t pool_capacity = engine.get_pool_capacity();

      // 3. Evaluate memory pool backpressure directly on the core side
      core::MetricsPacket packet{};
      if (pool_failures > 0 ||
          pool_used >= static_cast<size_t>(pool_capacity * 0.95)) [[unlikely]] {
        packet.type = core::MetricsUpdateType::BACKPRESSURE_DETECTED;
      } else {
        packet.type = core::MetricsUpdateType::ORDER_PROCESSED;
      }

      // 4. Populate the remaining telemetry payload
      packet.latency_cycles = elapsed_cycles;
      packet.total_orders = engine.get_total_orders();
      packet.total_trades = engine.get_total_trades();
      packet.pool_used = pool_used;
      packet.pool_failures = pool_failures;
      packet.pool_peak = engine.get_pool_peak();
      packet.pool_capacity = pool_capacity;

      // 5. Unidirectional drop into the lock-free SPSC queue
      metrics_queue->push(packet);
    }
  });

  if (core_thread.joinable()) {
    core_thread.join();
  }

  // Gracefully clean up threads and references
  worker.shutdown();
  std::cout << "[Shutdown Complete] Exited cleanly." << std::endl;

  return 0;
}
