#pragma once

#include <cstdint>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;
using SideType = std::uint8_t;

enum class Side : SideType { Buy, Sell };

struct Order {
    OrderId id = 0;
    Price price = 0;
    Quantity quantity = 0;
    Side side = Side::Buy;
};

struct Trade {
    OrderId buy_order_id = 0;
    OrderId sell_order_id = 0;
    Price price = 0;
    Quantity quantity = 0;
};

} // namespace lob