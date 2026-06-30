#pragma once

#include <lob/domain_types.hpp>

#include <type_traits>

namespace lob {

struct Trade {
    OrderId aggressive_order_id;
    OrderId resting_order_id;

    Price price;
    Quantity quantity;

    Side aggressive_side;
};
static_assert(std::is_trivially_copyable_v<Trade>);
static_assert(sizeof(Trade) == 40, "Trade struct is supposed to be 40 bytes in size");

} // namespace lob