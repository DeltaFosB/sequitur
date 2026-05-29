#include <sequitur/core/MatchingEngine.hpp>
#include <sequitur/utils/MetricsWorker.hpp>
#include <x86intrin.h>

namespace sequitur {
namespace core {
void MatchingEngine::submit_order(uint8_t side, uint64_t price,
                                  uint32_t quantity) {
  // 1. Hardware Entry Fence: Force all pipeline instructions to completely
  // drain and finish before reading our initial start cycle counter value.
  __builtin_ia32_lfence();
  uint64_t start_cycles = __rdtsc();

  total_orders++;
  Order *order = pool.acquire();
  order->side = side;
  order->price = price;
  order->quantity = quantity;
  order->id = next_order_id++;

  book->match_order(order, exec_q, trash_q);

  for (auto *dead_order : trash_q) {
    pool.release(dead_order);
  }

  trash_q.clear();

  if (order->quantity == 0) {
    pool.release(order);
  }
  exec_q.clear();

  // 2. Hardware Exit Fence: Force the entire order matching block to finish
  // processing and store its state before capturing our trailing cycle count.
  __builtin_ia32_lfence();
  uint64_t end_cycles = __rdtsc();

  // 3. Asynchronously push the recorded timeline down to the worker out-of-lock
  if (metrics_worker_) [[likely]] {
    metrics_worker_->record_latency(end_cycles - start_cycles);
  }
}
} // namespace core
} // namespace sequitur
