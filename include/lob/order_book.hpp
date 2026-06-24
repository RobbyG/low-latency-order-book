#pragma once

#include <lob/spsc_ring.hpp>
#include <lob/thread_util.hpp>

#include <compare>
#include <concepts>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <span>
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

inline constexpr std::size_t trade_ring_capacity = 1 << 17;
inline constexpr std::size_t command_ring_capacity = 1 << 15;
using TradeRing = SpscRing<Trade, trade_ring_capacity>;
using CommandRing = SpscRing<Command, command_ring_capacity>;

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

struct NewOrder {
    OrderId id;
    Price price;
    Quantity quantity;
    StpId stp_id; // placed here for byte alingment purposes

    Side side;
    OrderType order_type;
    TimeInForce time_in_force;
    SelfTradeResolve self_trade_resolve;
};

static_assert(sizeof(NewOrder) == 32, "NewOrder must be 32 bytes in size");
static_assert(alignof(NewOrder) == 8);

struct ValidatedNewOrder {
    NewOrder order;
};

static_assert(sizeof(ValidatedNewOrder) == 32, "ValidatedNewOrder must be 32 bytes in size");

struct RestingOrder {
    OrderId id;
    Quantity quantity;
    StpId stp_id;
    TimeInForce time_in_force;
};

static_assert(sizeof(RestingOrder) == 24, "RestingOrder must be 24 bytes in size");

struct Trade {
    OrderId aggressive_order_id;
    OrderId resting_order_id;

    Price price;
    Quantity quantity;

    Side aggressive_side;
};

class EventWriter final {
  public:
    explicit EventWriter(TradeRing &trade_ring) noexcept : trade_ring_(trade_ring) {}

    void on_trade(const Trade &trade) noexcept {
        while (!trade_ring_.push(trade)) {
            pause_cpu();
        }
    }

  private:
    TradeRing &trade_ring_;
};

enum class AddStatus : std::uint8_t {
    Accepted,

    InvalidQuantity,
    InvalidPrice,

    InvalidOrderType,
    InvalidTimeInForce,
    InvalidOrderTypeTimeInForce,

    DuplicateOrderId,
    BookFull,

    WouldNotFullyFill,
    UnsupportedTimeInForce
};

struct AddResult {
    Quantity filled;
    Quantity remaining;
    std::uint32_t trade_count;
    AddStatus status;

    [[nodiscard]] bool accepted() const noexcept {
        return status == AddStatus::Accepted;
    }

    [[nodiscard]] bool rejected() const noexcept {
        return !accepted();
    }
};

enum class CancelStatus : std::uint8_t { Cancelled, NotFound };

struct CancelResult {
    Quantity quantity;
    CancelStatus status;

    [[nodiscard]] bool cancelled() const noexcept {
        return status == CancelStatus::Cancelled;
    }
};

enum class ReduceStatus : std::uint8_t { Reduced, Cancelled, NotFound, InvalidQuantity };

struct ReduceResult {
    Quantity old_quantity;
    Quantity new_quantity;

    ReduceStatus status;

    [[nodiscard]] bool changed() const noexcept {
        return status == ReduceStatus::Reduced || status == ReduceStatus::Cancelled;
    }
};

enum class ReplaceStatus : std::uint8_t {
    Replaced,
    NotFound,

    InvalidQuantity,
    InvalidPrice,

    InvalidOrderType,
    InvalidTimeInForce,
    InvalidOrderTypeTimeInForce,

    DuplicateOrderId,
    BookFull,

    WouldNotFullyFill,
};

struct ReplaceResult {
    Quantity filled;
    Quantity remaining;
    std::uint32_t trade_count;
    ReplaceStatus status;

    [[nodiscard]] bool replaced() const noexcept {
        return status == ReplaceStatus::Replaced;
    }

    [[nodiscard]] bool rested() const noexcept {
        return replaced() && remaining.get_value() != 0;
    }

    [[nodiscard]] bool failed() const noexcept {
        return !replaced() && !rested();
    }
};

struct CopyResult {
    std::uint32_t written;
    std::uint32_t available;

    [[nodiscard]] bool truncated() const noexcept {
        return written < available;
    }
};

enum class CommandType : std::uint8_t { None, Add, Cancel, Reduce, Replace };

struct AddCmd {
    NewOrder order;
};

struct CancelCmd {
    OrderId id;
};

struct ReduceCmd {
    OrderId id;
    Quantity new_quantity;
};

struct ReplaceCmd {
    OrderId id;
    NewOrder new_order;
};

struct Command {
    CommandType type;

    union {
        std::byte dummy;
        NewOrder add;
        CancelCmd cancel;
        ReduceCmd reduce;
        ReplaceCmd replace;
    };

    Command() noexcept : type(CommandType::None), dummy{} {}

    static Command make_add(NewOrder order) noexcept {
        Command cmd;
        cmd.type = CommandType::Add;
        cmd.add = order;
        return cmd;
    }

    static Command make_cancel(CancelCmd cancel) noexcept {
        Command cmd;
        cmd.type = CommandType::Cancel;
        cmd.cancel = cancel;
        return cmd;
    }

    static Command make_reduce(ReduceCmd reduce) noexcept {
        Command cmd;
        cmd.type = CommandType::Reduce;
        cmd.reduce = reduce;
        return cmd;
    }

    static Command make_replace(ReplaceCmd replace) noexcept {
        Command cmd;
        cmd.type = CommandType::Replace;
        cmd.replace = replace;
        return cmd;
    }
};

static_assert(std::is_trivially_copyable_v<Trade>);
static_assert(std::is_trivially_copyable_v<NewOrder>);
static_assert(std::is_trivially_copyable_v<CancelCmd>);
static_assert(std::is_trivially_copyable_v<ReduceCmd>);
static_assert(std::is_trivially_copyable_v<ReplaceCmd>);
static_assert(std::is_trivially_copyable_v<Command>);

struct BestOrder {
    bool has_order;
    Price price;
    Side side;
};

struct PriceLevel {
    Price price;
    Quantity total_quantity;
    std::uint32_t order_count;
    Side side;
};

struct OrderView {
    OrderId id;
    Price price;
    Quantity quantity;
    StpId stp_id;
    Side side;
    TimeInForce time_in_force;
};

class OrderBook final {

  public:
    OrderBook() = default;

    OrderBook(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &) = delete;

    OrderBook(OrderBook &&) = delete;
    OrderBook &operator=(OrderBook &&) = delete;

    void reserve(std::uint32_t max_orders, std::uint32_t max_price_levels);
    void clear() noexcept;

    [[nodiscard]] AddResult add_order(const NewOrder &order, EventWriter &events) noexcept;
    [[nodiscard]] CancelResult cancel_order(OrderId id) noexcept;
    [[nodiscard]] ReduceResult reduce_order_by(OrderId id, Quantity quantity) noexcept;
    [[nodiscard]] ReplaceResult replace_order(OrderId id, const NewOrder &order,
                                              EventWriter &events) noexcept;

    [[nodiscard]] bool best_bid(BestOrder &out) const noexcept;
    [[nodiscard]] bool best_ask(BestOrder &out) const noexcept;
    [[nodiscard]] bool best_order(Side side, BestOrder &out) const noexcept;

    [[nodiscard]] CopyResult copy_bid_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_ask_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_depth(Side side, std::span<PriceLevel> out) const noexcept;

    [[nodiscard]] bool find_order(OrderId id, OrderView &out) const noexcept;
    [[nodiscard]] CopyResult copy_best_bid_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_best_ask_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_orders_at_price(Side side, Price price,
                                                  std::span<OrderView> out) const noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::uint32_t order_count() const noexcept;
    [[nodiscard]] std::uint32_t price_level_count(Side side) const noexcept;

#ifndef NDEBUG
    void validate() const;
#endif

  private:
    [[nodiscard]] AddStatus validate_new_order(const NewOrder &order) const noexcept;
    [[nodiscard]] AddResult add_validated_order(const ValidatedNewOrder &order,
                                                EventWriter &events) noexcept;
};
} // namespace lob