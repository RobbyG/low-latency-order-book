#pragma once

#include <lob/domain_types.hpp>
#include <lob/order.hpp>

#include <cstdint>

namespace lob {
enum class AddStatus : std::uint8_t {
    Accepted,

    InvalidQuantity,
    InvalidPrice,

    InvalidOrderType,
    InvalidTimeInForce,
    InvalidOrderTypeTimeInForce,

    DuplicateOrderId,
    BookFull,

    WouldNotFullyFill
};

struct AddResult {
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
static_assert(sizeof(AddResult) <= 16, "AddResult size must be <= 16 bytes");

enum class CancelStatus : std::uint8_t { Cancelled, NotFound };

struct CancelResult {
    Quantity quantity;
    CancelStatus status;

    [[nodiscard]] bool cancelled() const noexcept {
        return status == CancelStatus::Cancelled;
    }
};
static_assert(sizeof(CancelResult) <= 16, "CancelResult size must be <= 16 bytes");

enum class ReduceStatus : std::uint8_t { Reduced, Cancelled, NotFound, InvalidQuantity };

struct ReduceResult {
    Quantity old_quantity;
    Quantity new_quantity;

    ReduceStatus status;

    [[nodiscard]] bool changed() const noexcept {
        return status == ReduceStatus::Reduced || status == ReduceStatus::Cancelled;
    }
};
static_assert(sizeof(ReduceResult) <= 24, "ReduceResult size must be <= 24 bytes");

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
        return !replaced();
    }
};
static_assert(sizeof(ReplaceResult) <= 18, "ReplaceResult size must be <= 18 bytes");

struct CopyResult {
    std::uint32_t written;
    std::uint32_t available;

    [[nodiscard]] bool truncated() const noexcept {
        return written < available;
    }
};

// L1 view
struct BestOrder {
    Price price;
    Side side;
    bool has_order;
};
static_assert(sizeof(BestOrder) == 16, "BestOrder size must be 16 bytes");

// L2 view
struct PriceLevel {
    Price price;
    Quantity total_quantity;
    std::uint32_t order_count;
    Side side;
};
static_assert(sizeof(PriceLevel) == 24, "PriceLevel size must be 24 bytes");

// L3 view
struct OrderView {
    OrderId id;
    Price price;
    Quantity quantity;
    StpId stp_id;
    Side side;
    TimeInForce time_in_force;
    SelfTradeResolve self_trade_resolve;
};
static_assert(sizeof(OrderView) == 32, "OrderView size must be 32 bytes");

} // namespace lob