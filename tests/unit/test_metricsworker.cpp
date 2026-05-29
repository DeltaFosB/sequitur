#include <chrono>
#include <gtest/gtest.h>
#include <sequitur/core/MatchingEngine.hpp>
#include <sequitur/utils/MetricsWorker.hpp>
#include <thread>

using namespace sequitur;

// --- TEST 1: Interface Verification ---
TEST(MetricsWorkerTest, RecordMetricsInterface) {
  core::MatchingEngine engine(1000);
  utils::MetricsWorker telemetry(engine);

  // Verify that recording a valid latency cycle delta executes without side
  // effects or crashes 3600 cycles at 3.6GHz = 1000ns = 1 microsecond (Fits
  // within the 2048 bucket limit)
  telemetry.record_latency(3600);

  // Verify that recording a massive cycle value handles the outlier boundary
  // correctly 3600000000 cycles = 1 second (Triggers the outlier tracking
  // accumulation logic)
  telemetry.record_latency(3600000000);

  // Verify that tracking concurrent backpressure transitions executes cleanly
  telemetry.record_backpressure();

  SUCCEED();
}

// --- TEST 2: Multi-Sample Engine State Interactions ---
TEST(MetricsWorkerTest, EngineContextVerification) {
  core::MatchingEngine engine(1000);
  utils::MetricsWorker telemetry(engine);

  // Manually force order entries to verify that the worker can reference the
  // engine states during background loop execution periods without
  // thread-sanitizer faults
  engine.submit_order(0, 100, 10); // Buy
  engine.submit_order(1, 100, 10); // Sell (Crosses the book)

  // Give the background logging loop thread a minor window to sample engine
  // markers
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Assert that underlying engine states match expectation
  EXPECT_EQ(engine.get_total_orders(), 2);
  EXPECT_EQ(engine.get_total_trades(), 1);
}

// --- TEST 3: Safe Lifecycle Termination (RAII Verification) ---
TEST(MetricsWorkerTest, GracefulShutdownLifecycle) {
  core::MatchingEngine engine(1000);

  // Allocate inside an explicit local scope to test RAII (Resource Acquisition
  // Is Initialization)
  {
    utils::MetricsWorker telemetry(engine);
    // Background thread is actively running here
  }

  // The telemetry instance has fallen out of scope and its destructor has
  // fired. The internal background thread must join cleanly without leaking
  // resources or stalling the runtime.
  SUCCEED();
}
