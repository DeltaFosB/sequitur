#include <atomic>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <sequitur/concurrency/SPSCQueue.hpp>
#include <sequitur/core/EgressPacket.hpp>
#include <sequitur/core/IngressPacket.hpp>
#include <sequitur/core/MatchingEngine.hpp>
#include <sequitur/utils/MetricsWorker.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

// Simple inline assembly helper to read raw hardware clock cycles
inline uint64_t read_rdtsc() noexcept {
  unsigned int lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

// Global flag to manage graceful shutdowns from run_pipeline.sh
std::atomic<bool> engine_active{true};

void signal_handler(int) {
  engine_active.store(false, std::memory_order_release);
}

int main() {
  using namespace sequitur;

  // Register signal handlers for graceful teardown
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // --- 1. Map the POSIX Shared Memory Segment ---
  constexpr size_t RING_SIZE = 65536;
  constexpr size_t INGRESS_RING_BYTES = RING_SIZE * sizeof(core::IngressPacket);
  constexpr size_t EGRESS_RING_BYTES = RING_SIZE * sizeof(core::EgressPacket);

  // Explicitly calculate layout geometry to match Go size invariants (6291488
  // Bytes)
  constexpr size_t SHM_SIZE =
      8 + 8 + 8 + 8 + INGRESS_RING_BYTES + EGRESS_RING_BYTES;

  int shm_fd = shm_open("sequitur_shm", O_RDWR, 0666);
  if (shm_fd == -1) {
    std::cerr << "[Fatal Error] Failed to open /dev/shm/sequitur_shm. Ensure "
                 "the Go gateway is running."
              << std::endl;
    return 1;
  }

  void *shm_base =
      mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (shm_base == MAP_FAILED) {
    std::cerr << "[Fatal Error] Failed to mmap shared memory." << std::endl;
    close(shm_fd);
    return 1;
  }

  // Cast memory offsets directly to hardware atomic pointers matching the
  // expanded Go layout
  char *base_ptr = static_cast<char *>(shm_base);

  // Prefixed Ingress Shared Memory Control Cursors to perfectly match Go naming
  // schemes
  auto *ingress_read_idx = reinterpret_cast<std::atomic<int64_t> *>(base_ptr);
  auto *ingress_write_idx =
      reinterpret_cast<std::atomic<int64_t> *>(base_ptr + 8);
  auto *egress_read_idx =
      reinterpret_cast<std::atomic<int64_t> *>(base_ptr + 16);
  auto *egress_write_idx =
      reinterpret_cast<std::atomic<int64_t> *>(base_ptr + 24);

  // Array Base Starting Addresses with Ingress prefixes
  auto *ingress_ring_buffer =
      reinterpret_cast<core::IngressPacket *>(base_ptr + 32);
  auto *egress_ring_buffer = reinterpret_cast<core::EgressPacket *>(
      base_ptr + 32 + INGRESS_RING_BYTES);

  // --- 2. Initialize Telemetry & Egress Comm Channels ---
  auto metrics_queue =
      std::make_shared<concurrency::SPSCQueue<core::MetricsPacket, 4096>>();
  auto egress_queue =
      std::make_shared<concurrency::SPSCQueue<core::EgressPacket, 8192>>();

  utils::MetricsWorker worker(metrics_queue);

  std::cout
      << "[System Initialized] Hooked to /dev/shm. Pipeline channels decoupled."
      << std::endl;

  // --- 3. The Live Matching Core Thread ---
  std::thread core_thread([metrics_queue, egress_queue, ingress_read_idx,
                           ingress_write_idx, ingress_ring_buffer]() {
    core::MatchingEngine engine(1000000);

    // Sync our local consumer tracker with the shared memory
    int64_t current_read = ingress_read_idx->load(std::memory_order_relaxed);

    // Step 2 Local Optimization: Thread-Local Telemetry Accumulators
    constexpr uint64_t BATCH_WINDOW = 1024;
    uint64_t iteration_counter = 0;
    uint64_t rolling_cycles_sum = 0;

    // The Ultimate Hot Loop
    while (engine_active.load(std::memory_order_relaxed)) {
      int64_t current_write =
          ingress_write_idx->load(std::memory_order_acquire);

      if (current_read != current_write) {
        uint64_t start_cycles = read_rdtsc();

        // Calculate bitwise slot offset and read the packet
        size_t slot = current_read & (RING_SIZE - 1);
        const core::IngressPacket &packet = ingress_ring_buffer[slot];

        // Zero-copy local reference captures for execution processing
        uint8_t processed_side = packet.side;
        uint32_t processed_instrument = packet.instrument_id;
        uint32_t processed_trader = packet.trader_id;
        uint64_t processed_client_order_id = packet.client_order_id;

        // Execute matching logic natively
        if (packet.type == core::PacketType::NEW_ORDER) {
          engine.submit_order(packet.side, packet.price, packet.quantity);
        }

        // Stop the timing window IMMEDIATELY after execution
        uint64_t end_cycles = read_rdtsc();
        uint64_t elapsed_cycles = end_cycles - start_cycles;

        // Hardware memory release: Tell Go we finished reading this slot
        current_read++;
        ingress_read_idx->store(current_read, std::memory_order_release);

        // --- Step 1: Populate and Push Outbound Execution Report ---
        core::EgressPacket report_packet{};
        report_packet.type = core::EgressType::ORDER_ACCEPTED;
        report_packet.side = processed_side;
        report_packet.instrument_id = processed_instrument;
        report_packet.client_order_id = processed_client_order_id;
        report_packet.maker_id =
            processed_trader; // Map originating owner context to Maker profile
        report_packet.taker_id =
            processed_trader; // Map originating owner context to Taker profile
        report_packet.match_price = packet.price;
        report_packet.match_quantity = packet.quantity;

        if (!egress_queue->push(report_packet)) [[unlikely]] {
          std::clog << "[Egress Alert] Execution report dropped due to SPSC "
                       "saturation bounds."
                    << "\n";
        }

        // --- Step 2 Optimization: Macro-Batch Telemetry Verification Loop ---
        rolling_cycles_sum += elapsed_cycles;
        iteration_counter++;

        if (iteration_counter >= BATCH_WINDOW) [[unlikely]] {
          size_t pool_used = engine.get_pool_used();
          size_t pool_failures = engine.get_pool_failures();
          size_t pool_capacity = engine.get_pool_capacity();

          core::MetricsPacket m_packet{};
          if (pool_failures > 0 ||
              pool_used >= static_cast<size_t>(pool_capacity * 0.95)) {
            m_packet.type = core::MetricsUpdateType::BACKPRESSURE_DETECTED;
          } else {
            m_packet.type = core::MetricsUpdateType::ORDER_PROCESSED;
          }

          // Compute average latency across the execution window block
          m_packet.latency_cycles = rolling_cycles_sum / BATCH_WINDOW;
          m_packet.total_orders = engine.get_total_orders();
          m_packet.total_trades = engine.get_total_trades();
          m_packet.pool_used = pool_used;
          m_packet.pool_failures = pool_failures;
          m_packet.pool_peak = engine.get_pool_peak();
          m_packet.pool_capacity = pool_capacity;

          metrics_queue->push(m_packet);

          // Reset registers for the next macro block run
          iteration_counter = 0;
          rolling_cycles_sum = 0;
        }
      } else {
        // Spin-wait optimization: Instructs CPU we are idling inside a
        // spin-lock
        __builtin_ia32_pause();
      }
    }
  });

  // --- 3b. Asynchronous Egress Shared-Memory Publisher Thread ---
  std::thread egress_publisher_thread([egress_queue, egress_read_idx,
                                       egress_write_idx, egress_ring_buffer]() {
    core::EgressPacket outbound_packet;
    int64_t current_write = egress_write_idx->load(std::memory_order_relaxed);

    while (engine_active.load(std::memory_order_relaxed) ||
           !egress_queue->empty()) {
      if (egress_queue->pop(outbound_packet)) {
        int64_t current_read = egress_read_idx->load(std::memory_order_acquire);

        // Enforce structural backpressure if Go gateway processing falls behind
        while ((current_write - current_read) >= 65536) [[unlikely]] {
          if (!engine_active.load(std::memory_order_relaxed))
            break;
          __builtin_ia32_pause();
          current_read = egress_read_idx->load(std::memory_order_acquire);
        }

        size_t slot = current_write & (65536 - 1);
        egress_ring_buffer[slot] = outbound_packet;

        current_write++;
        egress_write_idx->store(current_write, std::memory_order_release);
      } else {
        std::this_thread::yield();
      }
    }
  });

  // --- 4. Keep Main Thread Alive to Allow Concurrency ---
  while (engine_active.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // --- 5. Graceful Teardown Sequence ---
  std::cout << "[Teardown Diagnostic] Main active signal killed. Rejoining "
               "running pipelines..."
            << std::endl;

  if (core_thread.joinable()) {
    core_thread.join();
  }

  if (egress_publisher_thread.joinable()) {
    egress_publisher_thread.join();
  }

  if (!metrics_queue->empty()) {
    std::cout << "[Teardown Diagnostic] Warning: SPSC Queue is shutting down "
                 "with unprocessed telemetry items inside!"
              << std::endl;
  }
  if (!egress_queue->empty()) {
    std::cout << "[Teardown Diagnostic] Warning: Egress queue closing with "
                 "residual execution reports trapped!"
              << std::endl;
  }

  munmap(shm_base, SHM_SIZE);
  close(shm_fd);
  worker.shutdown();

  std::cout << "[Shutdown Complete] Exited cleanly." << std::endl;

  return 0;
}
