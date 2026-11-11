#pragma once

#include <lob/domain_types.hpp>
#include <lob/order_book_results.hpp>
#include <lob/trade.hpp>

#include <variant>

namespace lob {

struct OrderAdded {
    OrderId id;
    Price price;
    Quantity quantity;
    Side side;
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

using EventType = std::variant<OrderAdded, OrderCancelled, OrderReduced, OrderRejected, Trade>;

struct MarketEvent {
    SequenceNumber seq;
    Timestamp t;
    EventType type;
};

static_assert(std::is_trivially_copyable_v<MarketEvent> &&
              "all even types must be trivially copyable");
static_assert(sizeof(MarketEvent) <= 64);
} // namespace lob