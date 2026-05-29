#pragma once
#include <cstdint>

namespace sequitur {
namespace core {

enum class PacketType : uint8_t { NEW_ORDER = 1, CANCEL_ORDER = 2 };

// Fixed-size, cache-aligned, zero-allocation network payload frame.
// Explicitly structured to minimize register-mapping transformation overhead.
struct alignas(32) IngressPacket {
  // Message Type / Instruction
  PacketType type; // 1 Byte

  // Market Fields (Direct Mapping to sequitur::core::Order)
  uint8_t side;           // 1 Byte  (0 = Buy, 1 = Sell)
  uint16_t padding16;     // 2 Bytes (Aligns next fields to 4-byte boundary)
  uint32_t instrument_id; // 4 Bytes
  uint32_t trader_id;     // 4 Bytes
  uint32_t quantity;      // 4 Bytes
  uint64_t price;         // 8 Bytes
  uint64_t
      client_order_id; // 8 Bytes (Maps directly to order->id or lookup key)
};

} // namespace core
} // namespace sequitur
