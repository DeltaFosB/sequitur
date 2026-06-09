#include <atomic>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <sequitur/concurrency/SPSCQueue.hpp>
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
  constexpr size_t SHM_SIZE = 8 + 8 + (RING_SIZE * sizeof(core::IngressPacket));

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

  // Cast memory offsets directly to hardware atomic pointers mapping the Go
  // layout
  auto *read_idx = reinterpret_cast<std::atomic<int64_t> *>(shm_base);
  auto *write_idx = reinterpret_cast<std::atomic<int64_t> *>(
      static_cast<char *>(shm_base) + 8);
  auto *ring_buffer = reinterpret_cast<core::IngressPacket *>(
      static_cast<char *>(shm_base) + 16);

  // --- 2. Initialize Telemetry Pipeline ---
  auto metrics_queue =
      std::make_shared<concurrency::SPSCQueue<core::MetricsPacket, 4096>>();

  utils::MetricsWorker worker(metrics_queue);

  std::cout << "[System Initialized] Hooked to /dev/shm. Telemetry decoupled."
            << std::endl;

  // --- 3. The Live Matching Core Thread ---
  std::thread core_thread([metrics_queue, read_idx, write_idx, ring_buffer]() {
    core::MatchingEngine engine(1000000);

    // Sync our local consumer tracker with the shared memory
    int64_t current_read = read_idx->load(std::memory_order_relaxed);

    // The Ultimate Hot Loop
    while (engine_active.load(std::memory_order_relaxed)) {
      int64_t current_write = write_idx->load(std::memory_order_acquire);

      if (current_read != current_write) {
        uint64_t start_cycles = read_rdtsc();

        // 1. Calculate bitwise slot offset and read the packet
        size_t slot = current_read & (RING_SIZE - 1);
        const core::IngressPacket &packet = ring_buffer[slot];

        // 2. Execute matching logic natively
        if (packet.type == core::PacketType::NEW_ORDER) {
          engine.submit_order(packet.side, packet.price, packet.quantity);
        }

        // FIXED: Stop the timing window IMMEDIATELY after execution
        uint64_t end_cycles = read_rdtsc();
        uint64_t elapsed_cycles = end_cycles - start_cycles;

        // 3. Hardware memory release: Tell Go we finished reading this slot
        current_read++;
        read_idx->store(current_read, std::memory_order_release);

        // 4. Extract hot memory pool metrics directly from local core state
        size_t pool_used = engine.get_pool_used();
        size_t pool_failures = engine.get_pool_failures();
        size_t pool_capacity = engine.get_pool_capacity();

        // 5. Evaluate memory pool backpressure on the core side
        [[maybe_unused]] core::MetricsPacket m_packet{};
        if (pool_failures > 0 ||
            pool_used >= static_cast<size_t>(pool_capacity * 0.95))
            [[unlikely]] {
          m_packet.type = core::MetricsUpdateType::BACKPRESSURE_DETECTED;
        } else {
          m_packet.type = core::MetricsUpdateType::ORDER_PROCESSED;
        }

        // 6. Populate the remaining telemetry payload
        m_packet.latency_cycles = elapsed_cycles;
        m_packet.total_orders = engine.get_total_orders();
        m_packet.total_trades = engine.get_total_trades();
        m_packet.pool_used = pool_used;
        m_packet.pool_failures = pool_failures;
        m_packet.pool_peak = engine.get_pool_peak();
        m_packet.pool_capacity = pool_capacity;

        // 7. Unidirectional drop into the lock-free SPSC queue (Out-of-band)
        // metrics_queue->push(m_packet);
      } else {
        // Spin-wait optimization: Instructs CPU we are idling inside a
        // spin-lock
        __builtin_ia32_pause();
      }
    }
  });

  // --- 4. Keep Main Thread Alive to Allow Concurrency ---
  // Instead of joining immediately and blocking main thread progression,
  // spin-wait here until a system termination signal flags engine_active to
  // false.
  while (engine_active.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // --- 5. Graceful Teardown Sequence ---
  if (core_thread.joinable()) {
    core_thread.join();
  }

  munmap(shm_base, SHM_SIZE);
  close(shm_fd);
  worker.shutdown();

  std::cout << "[Shutdown Complete] Exited cleanly." << std::endl;

  return 0;
}
