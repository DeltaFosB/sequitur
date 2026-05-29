#include <chrono>
#include <sequitur/core/MatchingEngine.hpp>
#include <sequitur/utils/MetricsWorker.hpp>
#include <thread>

int main() {
  sequitur::core::MatchingEngine engine(100000);

  sequitur::utils::MetricsWorker telemetry_worker(engine);

  for (uint64_t i = 0; i < 500000; ++i) {
    // Buy Side orders
    engine.submit_order(0, 100 + (i % 10), 10);

    // Sell Side matching targets
    engine.submit_order(1, 100 + (i % 10), 10);

    if (i % 10000 == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  return 0;
}
