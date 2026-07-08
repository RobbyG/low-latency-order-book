#pragma once

#include <cstddef>

namespace lob {

// capacity = slots in ring buffer, must be power of 2
// size of trade ring larger than command ring because there can be multiple trades per command
// can be adjusted later if needed

inline constexpr std::size_t trade_ring_capacity = 1 << 17; // 131072 - 1 trades
static_assert((trade_ring_capacity & (trade_ring_capacity - 1)) == 0,
              "trade_ring_capacity must be a power of 2");

inline constexpr std::size_t command_ring_capacity = 1 << 15; // 32768 - 1 commands
static_assert((command_ring_capacity & (command_ring_capacity - 1)) == 0,
              "command_ring_capacity must be a power of 2");

} // namespace lob