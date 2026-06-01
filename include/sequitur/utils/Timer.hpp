#pragma once

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h> // Provides access to ultra-fast hardware __rdtscp intrinsic
#endif

namespace sequitur {
namespace utils {

class Timer {
private:
#ifdef ENABLE_TELEMETRY
  uint64_t start_cycles;
  uint64_t *bfr;
  uint64_t *idx;
#endif

public:
  inline Timer([[maybe_unused]] uint64_t *buffer,
               [[maybe_unused]] uint64_t *index) noexcept {
#ifdef ENABLE_TELEMETRY
    bfr = buffer;
    idx = index;

    // Direct hardware instruction call (Takes only ~10-15 cycles total)
    unsigned int aux;
    start_cycles = __rdtscp(&aux);
#endif
  }

  inline ~Timer() noexcept {
#ifdef ENABLE_TELEMETRY
    unsigned int aux;
    uint64_t end_cycles = __rdtscp(&aux);

    // Record the RAW CPU cycles directly into the buffer array.
    // Zero nanosecond math conversions inside the hot execution window.
    bfr[(*idx)++] = (end_cycles - start_cycles);
#endif
  }
};

} // namespace utils
} // namespace sequitur
