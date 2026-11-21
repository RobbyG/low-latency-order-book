// Included by <lob/books/dense_ladder_ob.hpp>. Do not include directly.

#pragma once

#include <lob/books/dense_ladder_ob.hpp>
#include <lob/event_writer.hpp>
#include <lob/market_event.hpp>
#include <lob/trade.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace lob::books {

// construction

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
DenseLadderOrderBook<BandWidth, Hash>::DenseLadderOrderBook(Config config) : config_(config) {
    reserve(config_);
}

// API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
AddResult DenseLadderOrderBook<BandWidth, Hash>::add_order(const NewOrder &order,
                                                           EventWriter &event_writer) noexcept {
    assert(order.quantity != Quantity{0} && "a new order cannot have a quantity of 0");

    AddStatus status = validate_new_order(order, true);
    if (status != AddStatus::Accepted) {
        RejectReason reason;
        switch (status) {
        case AddStatus::DuplicateOrderId:
            reason = RejectReason::DuplicateOrderId;
            break;
        case AddStatus::WouldNotFullyFill:
            reason = RejectReason::FokNotFillable;
            break;
        }
        event_writer.on_event(Merket) return AddResult{
            .remaining = order.quantity, .status = status, .outcome = MatchOutcome::None};
    }

    return add_validated_order(order, event_writer);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CancelResult
DenseLadderOrderBook<BandWidth, Hash>::cancel_order(OrderId id,
                                                    EventWriter &event_writer) noexcept {
    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_resting_index)
        return CancelResult{.quantity = Quantity{0}, .status = CancelStatus::NotFound};

    const Quantity cancelled_quantity =
        resting_order_pool_[order_location_table_[slot].resting_index].quantity;

    cancel_order_at_slot(slot);

    return CancelResult{.quantity = cancelled_quantity, .status = CancelStatus::Cancelled};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
ReduceResult
DenseLadderOrderBook<BandWidth, Hash>::reduce_order_by(OrderId id, Quantity quantity,
                                                       EventWriter &event_writer) noexcept {

    const Quantity local_quantity = quantity;

    assert(local_quantity > Quantity{0} && "the quantity to reduce by must be greater than 0");
    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_resting_index)
        return ReduceResult{.new_quantity = Quantity{0},
                            .old_quantity = Quantity{0},
                            .status = ReduceStatus::NotFound};

    const OrderLocation entry = order_location_table_[slot];
    auto &order = resting_order_pool_[entry.resting_index];
    const Quantity old_quantity = order.quantity;

    if (old_quantity < local_quantity)
        return ReduceResult{.new_quantity = old_quantity,
                            .old_quantity = old_quantity,
                            .status = ReduceStatus::ExceedsRestingQuantity};

    if (old_quantity == local_quantity) {
        cancel_order_at_slot(slot);
        return ReduceResult{.new_quantity = Quantity{0},
                            .old_quantity = old_quantity,
                            .status = ReduceStatus::Cancelled};
    }

    Level &level = entry.side == Side::Buy ? get_level<Side::Buy>(entry.price)
                                           : get_level<Side::Sell>(entry.price);
    level.total_quantity -= local_quantity;

    const Quantity new_quantity = old_quantity - local_quantity;
    order.quantity = new_quantity;
    return ReduceResult{.new_quantity = new_quantity,
                        .old_quantity = old_quantity,
                        .status = ReduceStatus::Reduced};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
ReplaceResult
DenseLadderOrderBook<BandWidth, Hash>::replace_order(OrderId id, const NewOrder &order,
                                                     EventWriter &event_writer) noexcept {
    assert(order.quantity != Quantity{0} && "a new order cannot have a quantity of 0");

    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_resting_index)
        return ReplaceResult{.new_quantity = Quantity{0},
                             .add_status = AddStatus::NotAttempted,
                             .cancel_status = CancelStatus::NotFound,
                             .outcome = MatchOutcome::None};

    // if we replace with same id, skip duplicate check in validation as it will always find a
    // duplicate and fail the validation (the old order will be removed anyway)
    AddStatus add_status = validate_new_order(order, order.id != id);
    if (add_status != AddStatus::Accepted)
        return ReplaceResult{.new_quantity = Quantity{0},
                             .add_status = add_status,
                             .cancel_status = CancelStatus::NotAttempted,
                             .outcome = MatchOutcome::None};

    cancel_order_at_slot(slot);

    AddResult result = add_validated_order(order, event_writer);

    return ReplaceResult{.new_quantity = result.remaining,
                         .add_status = result.status,
                         .cancel_status = CancelStatus::Cancelled,
                         .outcome = result.outcome};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::end_session(EventWriter &event_writer) noexcept {}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
AddStatus
DenseLadderOrderBook<BandWidth, Hash>::validate_new_order(const NewOrder &order,
                                                          bool check_duplicate) const noexcept {
    if (check_duplicate) {
        std::size_t slot = find_order_location(order.id);
        if (slot != invalid_resting_index) {
            return AddStatus::DuplicateOrderId;
        }
    }

    if (order.time_in_force == TimeInForce::Fok && !can_fully_fill(order))
        return AddStatus::WouldNotFullyFill;

    return AddStatus::Accepted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
AddResult
DenseLadderOrderBook<BandWidth, Hash>::add_validated_order(const NewOrder &order,
                                                           EventWriter &event_writer) noexcept {
    Quantity remaining = order.quantity;
    MatchOutcome match_outcome;
    if (order.side == Side::Buy)
        match_outcome = order.stp_id != StpId{0}
                            ? match_order<Side::Buy, true>(remaining, order, event_writer)
                            : match_order<Side::Buy, false>(remaining, order, event_writer);
    else
        match_outcome = order.stp_id != StpId{0}
                            ? match_order<Side::Sell, true>(remaining, order, event_writer)
                            : match_order<Side::Sell, false>(remaining, order, event_writer);

    switch (match_outcome) {
    case MatchOutcome::Filled:
        assert(remaining == Quantity{0} && "Filled implies remaining quantity should be 0");
        return AddResult{.remaining = Quantity{0},
                         .status = AddStatus::Accepted,
                         .outcome = MatchOutcome::Filled};
    case MatchOutcome::Exhausted:
        if (order.order_type == OrderType::Limit && order.time_in_force != TimeInForce::Fok &&
            order.time_in_force != TimeInForce::Ioc)
            return order.side == Side::Buy ? rest_order<Side::Buy>(remaining, order)
                                           : rest_order<Side::Sell>(remaining, order);
        [[fallthrough]];
    case MatchOutcome::Aborted:
        return AddResult{.remaining = remaining,
                         .status = AddStatus::RemainderCancelled,
                         .outcome = match_outcome};
    }

    std::unreachable();
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_level(Level &level, Quantity &remaining,
                                                        Price level_price, const NewOrder &order,
                                                        EventWriter &event_writer) -> MatchOutcome {
    bool emit_trade;
    if (level.head == invalid_resting_index)
        return MatchOutcome::Exhausted;

    std::uint32_t resting_index = level.head;
    while (resting_index != invalid_resting_index && remaining > Quantity{0}) {
        emit_trade = true;
        std::uint32_t node_index_copy = resting_index;
        RestingOrderNode &node = resting_order_pool_[resting_index];

        if constexpr (StpActive) {
            if (order.stp_id == node.stp_id) {
                switch (order.self_trade_resolve) {
                case SelfTradeResolve::CancelBoth:
                    remove_from_order_index(node.id);
                    remove_resting_order(level, node, resting_index);

                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelNew:
                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelResting:
                    resting_index = node.next;
                    remove_from_order_index(node.id);
                    remove_resting_order(level, node, node_index_copy);
                    continue;
                case SelfTradeResolve::DecrementAndCancel:
                    emit_trade = false;
                    break;
                }
            }
        }

        resting_index = node.next;

        if (remaining >= node.quantity) {
            remaining -= node.quantity;
            remove_from_order_index(node.id);
            remove_resting_order(level, node,
                                 node_index_copy); // includes removing the quantity from
                                                   // the level.total_quantity
            if (emit_trade) {
                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = node.id,
                                  .price = level_price,
                                  .quantity = node.quantity,
                                  .aggressive_side = AggressiveSide};

                event_writer.on_event(trade);
            }
        } else {
            level.total_quantity -= remaining;
            if (emit_trade) {
                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = node.id,
                                  .price = level_price,
                                  .quantity = remaining,
                                  .aggressive_side = AggressiveSide};

                event_writer.on_event(trade);
            }
            node.quantity -= remaining;
            remaining = Quantity{0};
        }
    }

    if (remaining == Quantity{0})
        return MatchOutcome::Filled;
    else
        return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::match_order(Quantity &remaining,
                                                                const NewOrder &order,
                                                                EventWriter &event_writer) {
    Price limit = effective_limit(order);

    constexpr Side RestingSide = AggressiveSide == Side::Buy ? Side::Sell : Side::Buy;

    auto visit = [&](Level &level, Price price) {
        MatchOutcome outcome =
            match_level<AggressiveSide, StpActive>(level, remaining, price, order, event_writer);
        if (outcome == MatchOutcome::Exhausted)
            return WalkControl::Continue;
        else
            return WalkControl::Stop;
    };

    return walk_side<RestingSide>(limit, visit);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::scan_level(const Level &level, Price level_price,
                                                       const NewOrder &order,
                                                       Quantity &remaining) const noexcept
    -> MatchOutcome {
    if constexpr (StpActive) {
        const StpId stp_id = order.stp_id;
        const SelfTradeResolve stp_policy = order.self_trade_resolve;

        std::uint32_t node = level.head;

        while (node != invalid_resting_index) {
            const RestingOrderNode &resting = resting_order_pool_[node];
            const std::uint32_t next = resting.next;

            if (resting.stp_id == stp_id) {
                switch (stp_policy) {
                case SelfTradeResolve::CancelNew:
                case SelfTradeResolve::CancelBoth:
                    return MatchOutcome::Aborted;

                case SelfTradeResolve::CancelResting:
                    node = next;
                    continue;

                case SelfTradeResolve::DecrementAndCancel:
                    break;
                }
            }

            if (resting.quantity >= remaining)
                return MatchOutcome::Filled;

            remaining -= resting.quantity;
            node = next;
        }

    } else {
        Quantity available = level.total_quantity;

        if (available >= remaining)
            return MatchOutcome::Filled;

        remaining -= available;
    }

    return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S, bool StpActive>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(const NewOrder &order) const noexcept {
    Quantity remaining = order.quantity;
    const Price limit = effective_limit(order);

    auto visit = [&](const Level &level, Price price) {
        return scan_level<StpActive>(level, price, order, remaining);
    };

    return walk_side<S>(limit, visit) == MatchOutcome::Filled;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fully_fill(const NewOrder &order) const noexcept {
    if (order.side == Side::Buy)
        return order.stp_id != StpId{0} ? can_fill_levels<Side::Sell, true>(order)
                                        : can_fill_levels<Side::Sell, false>(order);
    else
        return order.stp_id != StpId{0} ? can_fill_levels<Side::Buy, true>(order)
                                        : can_fill_levels<Side::Buy, false>(order);
}

} // namespace lob::books