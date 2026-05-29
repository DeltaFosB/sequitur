#pragma once

#include <atomic>
#include <memory>
#include <sequitur/core/OrderBook.hpp>
#include <sequitur/memory/ObjectPool.hpp>

namespace sequitur {
namespace utils {
class MetricsWorker;
}
} // namespace sequitur

namespace sequitur {
namespace core {
class MatchingEngine {
private:
  memory::ObjectPool<Order> pool;
  std::unique_ptr<OrderBook> book;
  ExecutionQueue exec_q;
  GarbageQueue trash_q;
  uint64_t next_order_id;

  std::atomic<uint64_t> total_orders{0};
  utils::MetricsWorker *metrics_worker_{nullptr};

public:
  MatchingEngine(size_t pool_capacity)
      : pool(pool_capacity), book(std::make_unique<OrderBook>()),
        next_order_id(1) {}
  void submit_order(uint8_t side, uint64_t price, uint32_t quantity);

  void register_metrics_worker(utils::MetricsWorker *worker) noexcept {
    metrics_worker_ = worker;
  }

  uint64_t get_total_orders() const {
    return total_orders.load(std::memory_order_relaxed);
  }
  uint64_t get_total_trades() const { return book->get_total_trades(); }
  uint64_t get_total_volume() const { return book->get_total_volume(); }

  size_t get_pool_used() const { return pool.get_used_count(); }
  size_t get_pool_failures() const { return pool.get_alloc_failures(); }
  size_t get_pool_peak() const { return pool.get_peak_usage(); }
  size_t get_pool_capacity() const { return pool.get_capacity(); }
};
} // namespace core
} // namespace sequitur
