#pragma once
#include <cstdint>

namespace sequitur {
namespace core {

enum class EgressType : uint8_t {
  ORDER_ACCEPTED = 1,
  ORDER_REJECTED = 2,
  ORDER_CANCELED = 3,
  ORDER_FILLED = 4, // Triggered by a sequitur::core::Trade event
};

// Fixed-size, cache-aligned outbound execution report (64 Bytes)
// Aligned to match your 64-byte Order struct for balanced memory hierarchy
// cache blocks.
struct alignas(64) EgressPacket {
  EgressType type;        // 1 Byte  (Accepted, Filled, etc.)
  uint8_t side;           // 1 Byte  (0 = Buy, 1 = Sell)
  uint16_t padding16;     // 2 Bytes (Aligns next fields to 4-byte boundary)
  uint32_t instrument_id; // 4 Bytes

  // Identity Fields (Directly populated from your Trade / Order structs)
  uint64_t client_order_id; // 8 Bytes (The aggressive or resting order's
                            // identity key)
  uint64_t maker_id;        // 8 Bytes (Populated if type == ORDER_FILLED)
  uint64_t Skinner_id;      // 8 Bytes (Populated if type == ORDER_FILLED)

  // Execution Quantities and Pricing
  uint64_t match_price; // 8 Bytes (The exact price level the trade cleared at)
  uint32_t match_quantity;  // 4 Bytes (The volume matched in this specific
                            // transaction)
  uint32_t leaves_quantity; // 4 Bytes (The remaining open quantity resting in
                            // the book)

  uint8_t padding[16]; // Pad out to exactly 64 bytes to block L1 cache line
                       // bouncing
};

} // namespace core
} // namespace sequitur
