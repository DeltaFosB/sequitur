#include <gtest/gtest.h>
#include <sequitur/core/MatchingEngine.hpp>

using namespace sequitur;

// --- TEST 1: Absolute State Changes & Book Sweeping ---
TEST(CoreIntegrationTest, PriceTimePriorityAndBookSweeping) {
  // 1. Initialize matching engine with an isolated 100-slot object pool
  // allocation
  core::MatchingEngine engine(100);

  // Verify initial clear baseline engine metrics
  EXPECT_EQ(engine.get_total_orders(), 0);
  EXPECT_EQ(engine.get_total_trades(), 0);
  EXPECT_EQ(engine.get_total_volume(), 0);
  EXPECT_EQ(engine.get_pool_used(), 0);

  // 2. Submit resting maker sell orders to build passive book depth
  // API contract signature: submit_order(side [0=Buy, 1=Sell], price, quantity)
  engine.submit_order(1, 150, 100); // Order 1: Sell 100 units @ price 150
  engine.submit_order(1, 151, 100); // Order 2: Sell 100 units @ price 151

  // Assert that the two maker orders occupy memory blocks in the pool
  EXPECT_EQ(engine.get_total_orders(), 2);
  EXPECT_EQ(engine.get_pool_used(), 2);
  EXPECT_EQ(engine.get_total_trades(),
            0); // No matches should have occurred yet

  // 3. Fire an aggressive crossing taker buy order to trigger matching passes
  // Demanding 250 units at up to a price of 155. This must completely devour
  // both resting sell orders and leave 50 units resting on the bid side.
  engine.submit_order(0, 155, 250); // Taker Order: Buy 250 units @ price 155

  // 4. Assert Domain State Determinism
  // - Total submitted orders tracker increments to 3
  EXPECT_EQ(engine.get_total_orders(), 3);

  // - Two distinct matching trade transactions should have been recorded
  EXPECT_EQ(engine.get_total_trades(), 2);

  // - Cumulative trade volume matched must be exactly 200 (100 + 100)
  EXPECT_EQ(engine.get_total_volume(), 200);

  // - Memory Pool Invariant Check:
  // The two maker sell orders were completely filled and should be recycled
  // back to the pool. The aggressive taker buy order was partially filled for
  // 200 units, leaving 50 units resting in the book. Therefore, only exactly 1
  // order structure should occupy the pool.
  EXPECT_EQ(engine.get_pool_used(), 1);
  EXPECT_EQ(engine.get_pool_failures(), 0);
}

// --- TEST 2: Active Pool Slicing and Allocation Failures ---
TEST(CoreIntegrationTest, MemoryPoolExhaustionBackpressure) {
  // Instantiate an engine with a hard limit of exactly 2 allocation slots
  core::MatchingEngine engine(2);

  // Max out the underlying pool capacity with non-crossing orders
  engine.submit_order(0, 10, 10); // Slot 1
  engine.submit_order(0, 11, 10); // Slot 2

  EXPECT_EQ(engine.get_pool_used(), 2);
  EXPECT_EQ(engine.get_pool_failures(), 0);

  // Attempting a 3rd order submission must trigger an allocation failure within
  // your pool
  engine.submit_order(0, 12, 10);

  // Invariant verification: The core safely drops the tracking without crashing
  EXPECT_EQ(engine.get_pool_used(), 2);
  EXPECT_EQ(engine.get_pool_failures(), 1);
}
