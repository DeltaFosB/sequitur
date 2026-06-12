#include <sequitur/core/MatchingEngine.hpp>

namespace sequitur {
namespace core {

bool MatchingEngine::submit_order(uint8_t side, uint64_t price,
                                  uint32_t quantity, uint32_t trader_id) {
  Order *order = pool.acquire();

  if (order == nullptr) [[unlikely]] {
    return false;
  }

  total_orders++;

  order->side = side;
  order->price = price;
  order->quantity = quantity;
  order->trader_id = trader_id;
  order->id = next_order_id++;

  book->match_order(order, exec_q, trash_q);

  for (auto *dead_order : trash_q) {
    pool.release(dead_order);
  }
  trash_q.clear();

  if (order->quantity == 0) {
    pool.release(order);
  }

  return true;
}
} // namespace core
} // namespace sequitur
