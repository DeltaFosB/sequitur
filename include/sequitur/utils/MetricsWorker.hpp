#pragma once

#include <atomic>
#include <sequitur/core/MatchingEngine.hpp>
#include <thread>

namespace sequitur {
namespace utils {

class MetricsWorker {
private:
  const core::MatchingEngine &engine_;
  std::atomic<bool> active_{false};
  std::thread worker_thread_;
  void logging_loop();

public:
  MetricsWorker(const core::MatchingEngine &engine);
  ~MetricsWorker();
};
} // namespace utils
} // namespace sequitur
