#include <iostream>
#include <sequitur/core/MatchingEngine.hpp>
#include <sequitur/utils/MetricsWorker.hpp>

int main() {
  std::cout << "Initializing Sequitur High-Frequency Core..." << std::endl;

  // Allocate performance layouts out of the box
  sequitur::core::MatchingEngine engine(100000);
  sequitur::utils::MetricsWorker telemetry_worker(engine);

  std::cout << "Engine hot path initialized. Standing by for production I/O..."
            << std::endl;

  // Production execution pipeline loop or network gateway spin-up goes here

  return 0;
}
