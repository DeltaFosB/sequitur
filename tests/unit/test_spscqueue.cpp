#include <atomic>
#include <gtest/gtest.h>
#include <sequitur/concurrency/SPSCQueue.hpp>
#include <sequitur/core/EgressPacket.hpp>
#include <thread>

using namespace sequitur;

// --- TEST 1: Basic Functional Path ---
TEST(SPSCQueueTest, BasicPushPop) {
  // Explicitly specialize the template for EgressPacket and default capacity
  concurrency::SPSCQueue<core::EgressPacket, 2048> queue;

  // Zero-initialize the structure entirely to clear padding, then map explicit
  // values
  core::EgressPacket packet{};
  packet.type = core::EgressType::ORDER_ACCEPTED;
  packet.instrument_id = 1;
  packet.client_order_id = 55555;

  // Assert successful linear push
  ASSERT_TRUE(queue.push(packet));

  // Assert successful extraction via pass-by-reference out-parameter
  core::EgressPacket popped_packet{};
  ASSERT_TRUE(queue.pop(popped_packet));
  EXPECT_EQ(popped_packet.type, core::EgressType::ORDER_ACCEPTED);
  EXPECT_EQ(popped_packet.client_order_id, 55555);

  // Queue should clear cleanly
  EXPECT_TRUE(queue.empty());
}

// --- TEST 2: Capacity Boundary Backpressure ---
TEST(SPSCQueueTest, QueueFullBackpressure) {
  constexpr size_t TestCapacity = 16;
  concurrency::SPSCQueue<core::EgressPacket, TestCapacity> queue;

  core::EgressPacket packet{};
  packet.type = core::EgressType::ORDER_ACCEPTED;
  packet.instrument_id = 1;
  packet.client_order_id = 111;

  size_t pushed_count = 0;
  while (queue.push(packet)) {
    pushed_count++;
  }

  // Assert that your absolute distance optimization utilizes 100% of the
  // Capacity!
  EXPECT_EQ(pushed_count, TestCapacity);

  // The next push must confirm a true structural backpressure state
  EXPECT_FALSE(queue.push(packet));
}

// --- TEST 3: Strict One-to-One Thread Streaming ---
TEST(SPSCQueueTest, OneToOneProducerConsumerStreaming) {
  concurrency::SPSCQueue<core::EgressPacket, 2048> queue;

  const size_t total_packets = 50000;
  std::atomic<bool> start_signal{false};
  std::atomic<bool> producer_ready{false};

  // Spawn EXACTLY ONE dedicated producer thread (simulating the Engine Core
  // egress path)
  std::thread producer(
      [&queue, &start_signal, &producer_ready, total_packets]() {
        producer_ready.store(true, std::memory_order_release);

        // Block until the main thread drops the gate to isolate execution
        // timings.
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
  const size_t max_empty_spins = 1000000;
  core::EgressPacket consumer_packet{};

  while (processed_count < total_packets && empty_spins < max_empty_spins) {
    if (queue.pop(consumer_packet)) {
      EXPECT_EQ(consumer_packet.client_order_id, processed_count);
      EXPECT_EQ(consumer_packet.maker_id, 1001);
      EXPECT_EQ(consumer_packet.taker_id, 1010);
      processed_count++;
      empty_spins = 0; // Reset tracking on successful hit
    } else {
      empty_spins++;
      std::this_thread::yield(); // Let the producer work if the queue runs dry
    }
  }

  if (producer.joinable()) {
    producer.join();
  }

  // Assert absolute transmission consistency
  EXPECT_EQ(processed_count, total_packets);
  EXPECT_TRUE(queue.empty());
}
