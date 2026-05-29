#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <sequitur/core/Order.hpp>
#include <sequitur/core/Trade.hpp>
#include <sequitur/utils/FixedQueue.hpp>

namespace sequitur {
namespace core {

using ExecutionQueue = FixedQueue<Trade, 10000>;
using GarbageQueue = FixedQueue<Order *, 10000>;

static constexpr std::size_t MAX_PRICE_TICKS = 1000000;
struct PriceLevel {
  Order *head = nullptr, *tail = nullptr;
  uint32_t volume = 0;
};
class OrderBook {
private:
  std::atomic<uint64_t> total_trades{0};
  std::atomic<uint64_t> total_volume{0};

public:
  PriceLevel book[MAX_PRICE_TICKS + 1];
  uint64_t best_bid = 0, best_ask = MAX_PRICE_TICKS;

  void insert_order(Order *order) {
    uint64_t price = order->price;
    PriceLevel &pl = book[price];
    if (pl.head == nullptr) {
      pl.head = order;
      pl.tail = order;

      order->prev_order = nullptr;
      order->next_order = nullptr;
    } else {
      pl.tail->next_order = order;
      order->prev_order = pl.tail;

      pl.tail = order;
      order->next_order = nullptr;
    }

    pl.volume += order->quantity;

    if (order->side == 0)
      best_bid = std::max(price, best_bid);
    else if (order->side == 1)
      best_ask = std::min(price, best_ask);
  }

  void remove_order(Order *order) {
    PriceLevel &pl = book[order->price];

    if (order->prev_order == nullptr) {
      pl.head = order->next_order;
    } else {
      order->prev_order->next_order = order->next_order;
    }

    if (order->next_order == nullptr) {
      pl.tail = order->prev_order;
    } else {
      order->next_order->prev_order = order->prev_order;
    }

    pl.volume -= order->quantity;
  }

  void match_order(Order *incoming, ExecutionQueue &exec_q,
                   GarbageQueue &trash_q) {
    if (incoming->side == 0 && incoming->price >= best_ask) {
      while (incoming->quantity > 0 && best_ask <= incoming->price) {
        PriceLevel &pl = book[best_ask];
        if (pl.head == nullptr) {
          if (best_ask == MAX_PRICE_TICKS)
            break;
          best_ask++;
          continue;
        }

        Order *resting = pl.head;
        uint32_t trade_quantity =
            std::min(incoming->quantity, resting->quantity);

        total_trades.fetch_add(1, std::memory_order_relaxed);
        total_volume.fetch_add(trade_quantity, std::memory_order_relaxed);
        exec_q.push(
            {resting->id, incoming->id, resting->price, trade_quantity});

        incoming->quantity -= trade_quantity;
        resting->quantity -= trade_quantity;
        pl.volume -= trade_quantity;

        if (resting->quantity == 0) {
          trash_q.push(resting);
          remove_order(resting);
        }
      }
    } else if (incoming->side == 1 && incoming->price <= best_bid) {
      while (incoming->quantity > 0 && best_bid >= incoming->price) {
        PriceLevel &pl = book[best_bid];
        if (pl.head == nullptr) {
          if (best_bid == 0)
            break;
          best_bid--;
          continue;
        }
        Order *resting = pl.head;
        uint32_t trade_quantity =
            std::min(incoming->quantity, resting->quantity);

        total_trades.fetch_add(1, std::memory_order_relaxed);
        total_volume.fetch_add(trade_quantity, std::memory_order_relaxed);
        exec_q.push(
            {resting->id, incoming->id, resting->price, trade_quantity});

        incoming->quantity -= trade_quantity;
        resting->quantity -= trade_quantity;
        pl.volume -= trade_quantity;

        if (resting->quantity == 0) {
          trash_q.push(resting);
          remove_order(resting);
        }
      }
    }

    if (incoming->quantity > 0) {
      insert_order(incoming);
    }
  }
  uint64_t get_total_trades() const {
    return total_trades.load(std::memory_order_relaxed);
  }
  uint64_t get_total_volume() const {
    return total_volume.load(std::memory_order_relaxed);
  }
};
} // namespace core
} // namespace sequitur
