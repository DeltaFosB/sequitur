#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <sequitur/concurrency/SPSCQueue.hpp>
#include <sequitur/core/MetricsPacket.hpp>
#include <sequitur/utils/MetricsWorker.hpp>
#include <thread>

using namespace sequitur;

// --- TEST 1: Packet Consumption Path ---
TEST(MetricsWorkerTest, QueuePacketConsumptionLifecycle) {
  // Instantiate the shared SPSC telemetry queue via a shared_ptr
  auto shared_queue =
      std::make_shared<concurrency::SPSCQueue<core::MetricsPacket, 4096>>();

  // Worker context spins up autonomously using the clean shared pipeline
  // channel
  utils::MetricsWorker telemetry(shared_queue);

  // 1. Simulate a standard order processing metric packet (Valid bucket lookup)
  core::MetricsPacket order_packet{};
  order_packet.type = core::MetricsUpdateType::ORDER_PROCESSED;
  order_packet.latency_cycles =
      3600; // 3600 cycles at 3.6GHz = 1000ns (Fits in bucket 1000)
  order_packet.total_orders = 1;
  order_packet.total_trades = 0;
  order_packet.pool_used = 10;
  order_packet.pool_failures = 0;
  order_packet.pool_peak = 10;

  ASSERT_TRUE(shared_queue->push(order_packet));

  // 2. Simulate an outlier latency packet (Exceeds bucket limits)
  core::MetricsPacket outlier_packet{};
  outlier_packet.type = core::MetricsUpdateType::ORDER_PROCESSED;
  outlier_packet.latency_cycles =
      3600000000; // 3.6B cycles = 1,000,000,000ns (Outlier)
  outlier_packet.total_orders = 2;
  outlier_packet.total_trades = 1;
  outlier_packet.pool_used = 12;
  outlier_packet.pool_failures = 0;
  outlier_packet.pool_peak = 12;

  ASSERT_TRUE(shared_queue->push(outlier_packet));

  // 3. Simulate a memory pool backpressure event packet
  core::MetricsPacket bp_packet{};
  bp_packet.type = core::MetricsUpdateType::BACKPRESSURE_DETECTED;
  bp_packet.latency_cycles = 2500;
  bp_packet.total_orders = 3;
  bp_packet.total_trades = 1;
  bp_packet.pool_used = 950; // Triggers higher allocation bounds tracking
  bp_packet.pool_failures = 0;
  bp_packet.pool_peak = 950;

  ASSERT_TRUE(shared_queue->push(bp_packet));

  // Give the background processing loop thread a dedicated window to pop and
  // drain elements completely
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Assert that the pipeline was completely drained out-of-band by the worker
  // core thread
  EXPECT_TRUE(shared_queue->empty());

  telemetry.shutdown();
}

// --- TEST 2: Asynchronous Multi-Sample Verification ---
TEST(MetricsWorkerTest, AsynchronousStreamingContext) {
  auto shared_queue =
      std::make_shared<concurrency::SPSCQueue<core::MetricsPacket, 4096>>();
  utils::MetricsWorker telemetry(shared_queue);

  // Emulate sequential bursts from the single-threaded matching core dropping
  // data off out-of-band
  for (uint64_t i = 1; i <= 100; ++i) {
    core::MetricsPacket packet{};
    packet.type = core::MetricsUpdateType::ORDER_PROCESSED;
    packet.latency_cycles = 72; // ~20ns execution baseline window
    packet.total_orders = i;
    packet.total_trades = i / 2;
    packet.pool_used = i;
    packet.pool_failures = 0;
    packet.pool_peak = i;

    while (!shared_queue->push(packet)) {
      std::this_thread::yield(); // Protect against temporary internal ring
                                 // saturation
    }
  }

  // Allow the reader core to harvest updates mid-session
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  EXPECT_TRUE(shared_queue->empty());
  telemetry.shutdown();
}

// --- TEST 3: Safe Lifecycle Termination (RAII Verification) ---
TEST(MetricsWorkerTest, GracefulShutdownLifecycle) {
  auto shared_queue =
      std::make_shared<concurrency::SPSCQueue<core::MetricsPacket, 4096>>();

  // Allocate inside an explicit local block boundary context to test
  // deterministic RAII cleanup
  {
    utils::MetricsWorker telemetry(shared_queue);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Asynchronous processing loop thread is actively awake and running here
  }

  // Telemetry instance fell out of scope. Its destructor must call shutdown()
  // automatically, execute thread joins cleanly, and flag stop bits onto the
  // queue structure.
  core::MetricsPacket dummy_packet{};
  EXPECT_FALSE(shared_queue->push(
      dummy_packet)); // Queue shutdown must reject any new inbound components
}
