#pragma once

#include <lob/strong_types.hpp>

#include <cstdint>

namespace lob {

namespace tag {
struct OrderId {};
struct StpId {};

struct Price {};
struct Quantity {};
struct Notional {};
}; // namespace tag

using OrderId = IdType<std::uint64_t, tag::OrderId>;
using StpId = IdType<std::uint32_t, tag::StpId>;

using Price = Scalar<std::int64_t, tag::Price>;
using Quantity = Scalar<std::uint64_t, tag::Quantity>;
using Notional = Scalar<__int128, tag::Notional>;

enum class Side : std::uint8_t { Buy, Sell };
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

[[nodiscard]] constexpr Notional make_notional(Price price, Quantity quantity) noexcept {
    return Notional(static_cast<__int128>(price.get_value()) *
                    static_cast<__int128>(quantity.get_value()));
}

} // namespace lob