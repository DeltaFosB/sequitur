#pragma once

#include <cstddef>
#include <cstdint>

namespace sequitur {
namespace core {

enum class MetricsUpdateType : uint8_t {
  ORDER_PROCESSED = 1,
  BACKPRESSURE_DETECTED = 2
};

// Fixed-size, 64-byte cache-aligned data packet to stream telemetry out of the
// engine
struct alignas(64) MetricsPacket {
  MetricsUpdateType type; // 1 Byte
  uint8_t padding8[7];    // 7 Bytes (Aligns next fields to 8-byte boundaries)

  // Hardware Profiling Metrics
  uint64_t
      latency_cycles; // 8 Bytes (Raw RDTSC duration elapsed for the operation)

  // Asynchronous Snapshots of Core Counters
  uint64_t total_orders; // 8 Bytes
  uint64_t total_trades; // 8 Bytes

  // Object Pool Micro-states
  uint64_t pool_used;     // 8 Bytes
  uint64_t pool_failures; // 8 Bytes
  uint64_t pool_peak;     // 8 Bytes
  uint64_t pool_capacity; // 8 Bytes
};

} // namespace core
} // namespace sequitur
