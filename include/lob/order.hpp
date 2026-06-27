#pragma once

#include <lob/domain_types.hpp>

#include <cstdint>
#include <type_traits>

namespace lob {

enum class OrderType : std::uint8_t { Limit, Market };

enum class TimeInForce : std::uint8_t {
    Gtc, // good till cancel
    Ioc, // immediate or cancel
    Fok, // fill or kill
    Gfd  // good for day
};

enum class SelfTradeResolve : std::uint8_t {
    CancelNew,
    CancelResting,
    CancelBoth,
    DecrementAndCancel
};

struct RawNewOrder {
    OrderId id;
    Price price;
    Quantity quantity;
    StpId stp_id; // placed here for byte alignment purposes

    Side side;
    OrderType order_type;
    TimeInForce time_in_force;
    SelfTradeResolve self_trade_resolve;
};
static_assert(sizeof(RawNewOrder) == 32, "RawNewOrder must be 32 bytes in size");
static_assert(alignof(RawNewOrder) == 8);
static_assert(std::is_trivially_copyable_v<RawNewOrder>,
              "RawNewOrder must be trivially copyable for buffer ring usage");

struct NewOrder {
    OrderId id;
    Price price;
    Quantity quantity;
    StpId stp_id; // placed here for byte alignment purposes

    Side side;
    OrderType order_type;
    TimeInForce time_in_force;
    SelfTradeResolve self_trade_resolve;
};
static_assert(sizeof(NewOrder) == 32, "NewOrder must be 32 bytes in size");
static_assert(std::is_trivially_copyable_v<NewOrder>,
              "NewOrder must be trivially copyable for buffer ring usage");

struct RestingOrder {
    OrderId id;
    Quantity quantity;
    StpId stp_id;
    TimeInForce time_in_force;
};
static_assert(sizeof(RestingOrder) == 24, "RestingOrder must be 24 bytes in size");

} // namespace lob