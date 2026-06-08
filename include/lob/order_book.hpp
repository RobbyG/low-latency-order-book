#pragma once

#include <lob/order.hpp>

#include <compare>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob {

template <typename T, typename Tag> struct Scalar {
  public:
    constexpr Scalar() noexcept = default;
    explicit constexpr Scalar(T v) noexcept : value_(v) {}

    [[nodiscard]] friend constexpr bool operator==(Scalar, Scalar) noexcept = default;
    [[nodiscard]] friend constexpr auto operator<=>(Scalar, Scalar) noexcept = default;

    constexpr Scalar &operator-=(Scalar v) noexcept {
        value_ -= v.value_;
        return *this;
    }

    constexpr Scalar &operator+=(Scalar v) noexcept {
        value_ += v.value_;
        return *this;
    }

    [[nodiscard]] friend constexpr Scalar operator-(Scalar left, Scalar right) noexcept {
        left -= right;
        return left;
    }

    [[nodiscard]] friend constexpr Scalar operator+(Scalar left, Scalar right) noexcept {
        left += right;
        return left;
    }

    [[nodiscard]] constexpr T get_value() const noexcept {
        return value_;
    }

  private:
    T value_{};
};

struct OrderIdTag {};
struct StpIdTag {};
struct PriceTag {};
struct QuantityTag {};

using OrderId = Scalar<std::uint64_t, OrderIdTag>;
using StpId = Scalar<std::uint32_t, StpIdTag>;
using Price = Scalar<std::int64_t, PriceTag>;
using Quantity = Scalar<std::uint64_t, QuantityTag>;

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

struct alignas(32) NewOrder {
    OrderId id;
    Price price;
    Quantity quantity;
    StpId stp_id; // placed here for alingment purposes

    Side side;
    OrderType order_type;
    TimeInForce time_in_force;
    SelfTradeResolve self_trade_resolve;
};

struct RestingOrder {
    OrderId id;
    Quantity quantity;
    StpId stp_id;
};

class OrderBook {

  public:
#ifndef NDEBUG
    void validate() const;
#endif
};

} // namespace lob