#pragma once

#include <lob/domain_types.hpp>

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

} // namespace lob