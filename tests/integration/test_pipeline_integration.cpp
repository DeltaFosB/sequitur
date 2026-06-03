#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <sequitur/concurrency/MPSCQueue.hpp> // Using MPSC for IngressPacket frames
#include <sequitur/concurrency/SPSCQueue.hpp> // Using SPSC for EgressPacket frames
#include <sequitur/core/EgressPacket.hpp>
#include <sequitur/core/IngressPacket.hpp>
#include <sequitur/core/MatchingEngine.hpp>
#include <thread>

using namespace sequitur;

TEST(PipelineIntegrationTest, EndToEndSPSCPipelineSession) {
  const size_t total_packets = 5000;

  // Initialize core passive engine and asymmetric transit rings
  core::MatchingEngine engine(10000);

  // Explicitly specify compile-time template types to avoid CTAD deduction
  // failures
  concurrency::MPSCQueue ingress_queue;
  concurrency::SPSCQueue<core::EgressPacket, 8192> egress_queue;

  std::atomic<bool> trading_active{false};
  std::atomic<bool> ingress_ready{false};
  std::atomic<bool> egress_ready{false};
  std::atomic<size_t> total_egress_processed{0};

  // Spawn Outbound Network Line Handler (Egress SPSC Consumer)
  std::thread egress_worker([&egress_queue, &trading_active, &egress_ready,
                             &total_egress_processed]() {
    egress_ready.store(true, std::memory_order_release);
    while (!trading_active.load(std::memory_order_acquire))
      ;

    core::EgressPacket out_packet{};
    while (trading_active.load(std::memory_order_relaxed) ||
           !egress_queue.empty()) {
      // Use zero-copy out-parameter pop extraction
      if (egress_queue.pop(out_packet)) {
        total_egress_processed.fetch_add(1, std::memory_order_relaxed);
        EXPECT_NE(out_packet.match_price, 0);
      } else {
        std::this_thread::yield(); // Prevent aggressive core starvation on
                                   // empty cycles
      }
    }
  });

  // Spawn Inbound Gateway Thread (Ingress MPSC Producer)
  std::thread ingress_worker(
      [&ingress_queue, &trading_active, &ingress_ready, total_packets]() {
        ingress_ready.store(true, std::memory_order_release);
        while (!trading_active.load(std::memory_order_acquire))
          ;

        for (size_t i = 0; i < total_packets; ++i) {
          uint8_t side = (i % 2 == 0) ? 0 : 1;

          core::IngressPacket packet{};
          packet.type = core::PacketType::NEW_ORDER;
          packet.side = side;
          packet.instrument_id = 1;
          packet.trader_id = 101;
          packet.quantity = 10;
          packet.price = 1000 + (i % 5);
          packet.client_order_id = static_cast<uint64_t>(i);

          while (!ingress_queue.push(packet)) {
            std::this_thread::yield();
          }
        }
      });

  while (!ingress_ready.load(std::memory_order_relaxed) ||
         !egress_ready.load(std::memory_order_relaxed))
    ;

  trading_active.store(true, std::memory_order_release);

  // Inline Core Engine Thread execution loop
  size_t processed_ingress = 0;
  while (processed_ingress < total_packets) {
    if (auto ingress_packet = ingress_queue.pop()) {

      if (ingress_packet->type == core::PacketType::NEW_ORDER) {
        engine.submit_order(ingress_packet->side, ingress_packet->price,
                            ingress_packet->quantity);
      }

      core::EgressPacket report{};
      report.type = core::EgressType::ORDER_FILLED;
      report.side = ingress_packet->side;
      report.instrument_id = ingress_packet->instrument_id;
      report.client_order_id = ingress_packet->client_order_id;
      report.match_price = ingress_packet->price;
      report.match_quantity = ingress_packet->quantity;
      report.maker_id = ingress_packet->trader_id;

      // Pass by reference out-parameter push
      while (!egress_queue.push(report)) {
        std::this_thread::yield();
      }

      processed_ingress++;
    } else {
      std::this_thread::yield();
    }
  }

  trading_active.store(false, std::memory_order_release);

  if (ingress_worker.joinable())
    ingress_worker.join();
  if (egress_worker.joinable())
    egress_worker.join();

  EXPECT_EQ(processed_ingress, total_packets);
  EXPECT_EQ(engine.get_total_orders(), total_packets);
  EXPECT_EQ(total_egress_processed.load(), total_packets);
  EXPECT_EQ(engine.get_pool_failures(), 0);
}
