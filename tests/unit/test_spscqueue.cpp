#include <atomic>
#include <gtest/gtest.h>
#include <optional>
#include <sequitur/concurrency/SPSCQueue.hpp>
#include <sequitur/core/EgressPacket.hpp>
#include <thread>

using namespace sequitur;

// --- TEST 1: Basic Functional Path ---
TEST(SPSCQueueTest, BasicPushPop) {
  concurrency::SPSCQueue queue;

  // Zero-initialize the structure entirely to clear padding, then map explicit
  // values
  core::EgressPacket packet{};
  packet.type = core::EgressType::ORDER_ACCEPTED;
  packet.instrument_id = 1;
  packet.client_order_id = 55555;

  // Assert successful linear push
  ASSERT_TRUE(queue.push(packet));

  // Assert successful extraction and structural matching
  auto popped = queue.pop();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(popped->type, core::EgressType::ORDER_ACCEPTED);
  EXPECT_EQ(popped->client_order_id, 55555);

  // Queue should clear cleanly
  EXPECT_TRUE(queue.empty());
}

// --- TEST 2: Capacity Boundary Backpressure ---
TEST(SPSCQueueTest, QueueFullBackpressure) {
  concurrency::SPSCQueue queue;

  core::EgressPacket packet{};
  packet.type = core::EgressType::ORDER_ACCEPTED;
  packet.instrument_id = 1;
  packet.client_order_id = 111;

  // Dynamically saturate the SPSC ring buffer until it rejects an entry.
  // This satisfies the 1-cell dead property typical of lock-free power-of-2
  // rings.
  size_t pushed_count = 0;
  while (queue.push(packet)) {
    pushed_count++;
  }

  // With a capacity of 2048, the queue will cap exactly at 2047 items
  EXPECT_EQ(pushed_count, 2047);

  // The next push must return false due to ring wrap-around boundary detection
  EXPECT_FALSE(queue.push(packet));
}

// --- TEST 3: Strict One-to-One Thread Streaming ---
TEST(SPSCQueueTest, OneToOneProducerConsumerStreaming) {
  concurrency::SPSCQueue queue;

  const size_t total_packets = 50000;
  std::atomic<bool> start_signal{false};
  std::atomic<bool> producer_ready{false};

  // Spawn EXACTLY ONE dedicated producer thread (simulating the Engine Core
  // egress path)
  std::thread producer(
      [&queue, &start_signal, &producer_ready, total_packets]() {
        producer_ready.store(true, std::memory_order_release);

        // Block until the main thread drops the gate to isolate execution
        // timings. yield() ensures the core is released on resource-constrained
        // cloud environments.
        while (!start_signal.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }

        for (size_t i = 0; i < total_packets; ++i) {
          core::EgressPacket p{};
          p.type = core::EgressType::ORDER_FILLED;
          p.side = 1;
          p.instrument_id = 1;
          p.client_order_id = static_cast<uint64_t>(i);
          p.maker_id = 1001;
          p.taker_id = 1010;
          p.match_price = 5000;
          p.match_quantity = 10;

          // Step over backpressure states linearly if the consumer core falls
          // out of cache
          while (!queue.push(p)) {
            std::this_thread::yield();
          }
        }
      });

  // Wait for the background thread to spin up completely
  while (!producer_ready.load(std::memory_order_relaxed)) {
    std::this_thread::yield();
  }

  // Open the barrier!
  start_signal.store(true, std::memory_order_release);

  // Consumer execution loop (Simulating a single outbound Client Gateway
  // thread)
  size_t processed_count = 0;
  size_t empty_spins = 0;
  const size_t max_empty_spins = 500000;

  while (processed_count < total_packets && empty_spins < max_empty_spins) {
    if (auto packet = queue.pop()) {
      EXPECT_EQ(packet->client_order_id, processed_count);
      EXPECT_EQ(packet->maker_id, 1001);
      EXPECT_EQ(packet->taker_id, 1010);
      processed_count++;
      empty_spins = 0; // Reset tracking on successful hit
    } else {
      empty_spins++;
      std::this_thread::yield(); // Let the producer thread work if the queue
                                 // runs completely empty
    }
  }

  if (producer.joinable()) {
    producer.join();
  }

  // Assert absolute transmission consistency
  EXPECT_EQ(processed_count, total_packets);
  EXPECT_TRUE(queue.empty());
}
