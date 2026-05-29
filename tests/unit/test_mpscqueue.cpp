#include <atomic>
#include <gtest/gtest.h>
#include <optional>
#include <sequitur/concurrency/MPSCQueue.hpp>
#include <sequitur/core/IngressPacket.hpp>
#include <thread>
#include <vector>

using namespace sequitur;

// --- TEST 1: Basic Single-Threaded Functionality ---
TEST(MPSCQueueTest, BasicPushPop) {
  concurrency::MPSCQueue queue;

  // Use aggregate zero-initialization to bypass strict layout padding alerts
  core::IngressPacket packet{};
  packet.type = core::PacketType::NEW_ORDER;
  packet.trader_id = 101;
  packet.quantity = 50;
  packet.price = 9999;
  packet.client_order_id = 10001;

  // Assert successful push
  ASSERT_TRUE(queue.push(packet));

  // Assert successful extraction and structural integrity
  auto popped = queue.pop();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(popped->client_order_id, 10001);
  EXPECT_EQ(popped->price, 9999);
  EXPECT_EQ(popped->quantity, 50);

  // Queue should now be completely empty
  EXPECT_FALSE(queue.pop().has_value());
}

// --- TEST 2: Capacity Boundary and Backpressure ---
TEST(MPSCQueueTest, QueueFullBackpressure) {
  concurrency::MPSCQueue queue;

  core::IngressPacket packet{};
  packet.type = core::PacketType::NEW_ORDER;
  packet.trader_id = 101;
  packet.quantity = 10;
  packet.price = 100;
  packet.client_order_id = 1;

  // Dynamically saturate the MPSC ring buffer until it hits its exact
  // allocation wall. This abstracts away explicit slot offsets across variable
  // indexing strategies.
  size_t pushed_count = 0;
  while (queue.push(packet) && pushed_count < 4096) {
    pushed_count++;
  }

  // The queue is now completely full. The next push must return false,
  // proving that backpressure handling actively blocks overflows.
  EXPECT_FALSE(queue.push(packet));
}

// --- TEST 3: Multi-Producer Concurrent Ingestion Stress Test ---
TEST(MPSCQueueTest, ConcurrentProducerContention) {
  concurrency::MPSCQueue queue;

  const size_t num_producers = 4;
  const size_t packets_per_producer = 10000;
  const size_t total_expected_packets = num_producers * packets_per_producer;

  std::atomic<bool> start_signal{false};
  std::atomic<size_t> threads_ready{0};
  std::vector<std::thread> producers;

  // Spawn concurrent writers
  for (size_t i = 0; i < num_producers; ++i) {
    producers.emplace_back(
        [&queue, &start_signal, &threads_ready, packets_per_producer, i]() {
          // Signal that this thread is spawned and waiting
          threads_ready.fetch_add(1, std::memory_order_relaxed);

          // Spin-wait until the main thread releases the gate for simultaneous
          // collision
          while (!start_signal.load(std::memory_order_acquire))
            ;

          for (size_t j = 0; j < packets_per_producer; ++j) {
            core::IngressPacket p{};
            p.type = core::PacketType::NEW_ORDER;
            p.trader_id = static_cast<uint32_t>(i);
            p.quantity = 10;
            p.price = 100;
            p.client_order_id = static_cast<uint64_t>(j);

            // Keep pushing until it succeeds (spinning if hitting backpressure)
            while (!queue.push(p))
              ;
          }
        });
  }

  // Wait for all producer threads to line up at the gate
  while (threads_ready.load(std::memory_order_relaxed) < num_producers)
    ;

  // Open the gates! All threads smash the CAS loop simultaneously
  start_signal.store(true, std::memory_order_release);

  // Consumer side: Drain the queue concurrently while producers are writing
  size_t total_processed = 0;
  size_t empty_spins = 0;
  const size_t max_empty_spins = 100000; // Fail-safe fallback escape boundary

  while (total_processed < total_expected_packets &&
         empty_spins < max_empty_spins) {
    if (auto packet = queue.pop()) {
      total_processed++;
      empty_spins = 0; // Reset tracking on successful hit
    } else {
      empty_spins++;
    }
  }

  // Join producer threads
  for (auto &t : producers) {
    if (t.joinable())
      t.join();
  }

  // Validate that every single packet made it through without corruption or
  // drop
  EXPECT_EQ(total_processed, total_expected_packets);
  EXPECT_TRUE(queue.empty());
}
