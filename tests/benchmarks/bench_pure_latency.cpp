#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sequitur/core/MatchingEngine.hpp>
#include <vector>

#define TOTAL_ORDERS 1'000'000
#define BUCKET_SIZE 1'000
#define NUM_BUCKETS (TOTAL_ORDERS / BUCKET_SIZE)

using namespace sequitur::core;
using namespace std;

int main() {
  MatchingEngine engine(TOTAL_ORDERS);

  // Tracks the execution duration of each independent block of 1'000 orders
  std::vector<double> bucket_latencies_ns(NUM_BUCKETS, 0.0);

  // 1. Warm up the instruction cache and memory structures
  for (size_t w = 0; w < 10000; ++w) {
    engine.submit_order(0, 100, 10);
    engine.submit_order(1, 100, 10);
  }

  // 2. Execute Workload in Pure, Unpolluted Macro-Buckets
  auto global_start = std::chrono::high_resolution_clock::now();

  for (size_t b = 0; b < NUM_BUCKETS; b++) {
    // Measure this bucket from the OUTSIDE
    auto bucket_start = std::chrono::high_resolution_clock::now();

    // The Critical Path: 100% pure, un-instrumented loop execution
    for (size_t i = 0; i < BUCKET_SIZE / 2; i++) {
      engine.submit_order(0, 100, 10);
      engine.submit_order(1, 100, 10);
    }

    auto bucket_end = std::chrono::high_resolution_clock::now();
    uint64_t bucket_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             bucket_end - bucket_start)
                             .count();

    // Average order latency within this specific batch unit
    bucket_latencies_ns[b] = static_cast<double>(bucket_ns) / BUCKET_SIZE;
  }

  auto global_end = std::chrono::high_resolution_clock::now();
  uint64_t total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          global_end - global_start)
                          .count();

  // 3. Print Clean Results
  cout << "Total Orders Processed: " << engine.get_total_orders() << endl;
  cout << "Total Trades Executed: " << engine.get_total_trades() << endl;
  cout << "Total Volume Matched: " << engine.get_total_volume() << endl;
  cout << "Total Nanoseconds: " << total_ns << endl;

  double avg_latency = static_cast<double>(total_ns) / TOTAL_ORDERS;
  double throughput = TOTAL_ORDERS / (total_ns / 1'000'000'000.0);

  cout << "Pure Macro Average Latency: " << avg_latency << " ns" << endl;
  cout << "Throughput:                  " << throughput << " OPS" << endl;

  // 4. Extract Real Unpolluted P99
  std::sort(bucket_latencies_ns.begin(), bucket_latencies_ns.end());
  size_t p99_idx = static_cast<size_t>(bucket_latencies_ns.size() * 0.99);
  double p99_latency = bucket_latencies_ns[p99_idx];

  cout << "P99 Batch Latency:           " << p99_latency << " ns" << endl;

  return 0;
}
