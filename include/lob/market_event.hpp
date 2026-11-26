#pragma once

#include <lob/domain_types.hpp>
#include <lob/hardware_constants.hpp>
#include <lob/order_book_results.hpp>
#include <lob/trade.hpp>

#include <type_traits>
#include <variant>

namespace lob {

struct OrderRested {
    OrderId id;
    Price price;
    Quantity quantity;
    Side side;

    StpId stp_id;
    TimeInForce time_in_force;
};

struct OrderCancelled {
    OrderId id;
};

struct OrderReduced {
    OrderId id;
    Quantity new_quantity;
};

struct OrderRejected {
    OrderId id;
    RejectReason reason;
};

using EventType = std::variant<OrderRested, OrderCancelled, OrderReduced, OrderRejected, Trade>;

struct alignas(cache_line_size) MarketEvent {
    SequenceNumber seq;
    Timestamp t;
    EventType type;
};

static_assert(std::is_trivially_copyable_v<MarketEvent> &&
              "all even types must be trivially copyable");
static_assert(sizeof(MarketEvent) <= cache_line_size);
} // namespace lob