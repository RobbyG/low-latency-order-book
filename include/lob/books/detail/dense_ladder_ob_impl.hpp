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

// API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
AddResult DenseLadderOrderBook<BandWidth, Hash>::add_order(const NewOrder &order,
                                                           EventWriter &event_writer) noexcept {
    assert(order.quantity != Quantity{0} && "a new order cannot have a quantity of 0");

    AddStatus status = validate_new_order(order, true);
    if (status != AddStatus::Accepted) {
        event_writer.write_event(
            OrderRejected{.id = order.id, .reason = validation_reject_reason(status)});
        return AddResult{
            .remaining = order.quantity, .status = status, .outcome = MatchOutcome::None};
    }

    return add_validated_order(order, event_writer);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CancelResult
DenseLadderOrderBook<BandWidth, Hash>::cancel_order(OrderId id,
                                                    EventWriter &event_writer) noexcept {
    CancelResult result = this->remove_order(id);

    if (result.status == CancelStatus::Cancelled)
        event_writer.write_event(OrderCancelled{.id = id});
    else
        event_writer.write_event(OrderRejected{.id = id, .reason = RejectReason::UnknownOrderId});

    return result;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
ReduceResult
DenseLadderOrderBook<BandWidth, Hash>::reduce_order_by(OrderId id, Quantity quantity,
                                                       EventWriter &event_writer) noexcept {

    ReduceResult result = this->reduce_order(id, quantity);

    switch (result.status) {
    case ReduceStatus::Reduced:
        event_writer.write_event(OrderReduced{.id = id, .new_quantity = result.new_quantity});
        break;
    case ReduceStatus::Cancelled:
        event_writer.write_event(OrderCancelled{.id = id});
        break;
    case ReduceStatus::NotFound:
        event_writer.write_event(OrderRejected{.id = id, .reason = RejectReason::UnknownOrderId});
        break;
    case ReduceStatus::ExceedsRestingQuantity:
        event_writer.write_event(
            OrderRejected{.id = id, .reason = RejectReason::ExceedsRestingQuantity});
        break;
    }

    return result;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
ReplaceResult
DenseLadderOrderBook<BandWidth, Hash>::replace_order(OrderId id, const NewOrder &order,
                                                     EventWriter &event_writer) noexcept {
    assert(order.quantity != Quantity{0} && "a new order cannot have a quantity of 0");

    const std::size_t slot = this->find_order_location(id);

    if (slot == invalid_hash_slot) {
        event_writer.write_event(OrderRejected{.id = id, .reason = RejectReason::UnknownOrderId});
        return ReplaceResult{.new_quantity = Quantity{0},
                             .add_status = AddStatus::NotAttempted,
                             .cancel_status = CancelStatus::NotFound,
                             .outcome = MatchOutcome::None};
    }

    if (order.side != order_location_table_[slot].side) {
        event_writer.write_event(
            OrderRejected{.id = id, .reason = RejectReason::SideChangeNotAllowed});
        return ReplaceResult{.new_quantity = Quantity{0},
                             .add_status = AddStatus::NotAttempted,
                             .cancel_status = CancelStatus::NotAttempted,
                             .outcome = MatchOutcome::None};
    }

    // if we replace with same id, skip duplicate check in validation as it will always find a
    // duplicate and fail the validation (the old order will be removed anyway)
    AddStatus add_status = validate_new_order(order, order.id != id);
    if (add_status != AddStatus::Accepted) {
        event_writer.write_event(
            OrderRejected{.id = id, .reason = validation_reject_reason(add_status)});
        return ReplaceResult{.new_quantity = Quantity{0},
                             .add_status = add_status,
                             .cancel_status = CancelStatus::NotAttempted,
                             .outcome = MatchOutcome::None};
    }

    this->remove_order_from_location(slot);
    event_writer.write_event(OrderCancelled{.id = id});

    AddResult result = add_validated_order(order, event_writer);

    return ReplaceResult{.new_quantity = result.remaining,
                         .add_status = result.status,
                         .cancel_status = CancelStatus::Cancelled,
                         .outcome = result.outcome};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::end_session(EventWriter &event_writer) noexcept {
    eod_purge_ids_.clear();

    for (std::size_t slot = 0; slot <= this->hash_mask_; ++slot) {
        std::uint32_t resting_index = order_location_table_[slot].resting_index;
        if (resting_index != invalid_resting_index &&
            resting_order_pool_[resting_index].time_in_force == TimeInForce::Gfd)
            eod_purge_ids_.push_back(resting_order_pool_[resting_index].id);
    }

    for (const OrderId id : eod_purge_ids_) {
        (void)cancel_order(id, event_writer);
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_level(Level &level, Quantity &remaining,
                                                        Price level_price, const NewOrder &order,
                                                        EventWriter &event_writer) noexcept
    -> MatchOutcome {
    bool emit_trade;
    if (level.head == invalid_resting_index)
        return MatchOutcome::Exhausted;

    std::uint32_t resting_index = level.head;
    while (resting_index != invalid_resting_index && remaining > Quantity{0}) {
        emit_trade = true;
        std::uint32_t node_index_copy = resting_index;
        RestingOrderNode &node = resting_order_pool_[resting_index];
        const OrderId resting_id = node.id;
        const Quantity resting_quantity = node.quantity;

        if constexpr (StpActive) {
            if (order.stp_id == node.stp_id) {
                switch (order.self_trade_resolve) {
                case SelfTradeResolve::CancelBoth:
                    this->remove_resting_from_level(level, node, node_index_copy);
                    this->remove_slot_from_order_locations(resting_id);
                    event_writer.write_event(OrderCancelled{.id = resting_id});
                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelNew:
                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelResting:
                    resting_index = node.next;
                    this->remove_resting_from_level(level, node, node_index_copy);
                    this->remove_slot_from_order_locations(resting_id);
                    event_writer.write_event(OrderCancelled{.id = resting_id});
                    continue;
                case SelfTradeResolve::DecrementAndCancel:
                    emit_trade = false;
                    break;
                }
            }
        }

        resting_index = node.next;

        if (remaining >= resting_quantity) {
            remaining -= resting_quantity;
            this->remove_resting_from_level(level, node, node_index_copy);
            this->remove_slot_from_order_locations(resting_id);
            if (emit_trade) {
                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = resting_id,
                                  .price = level_price,
                                  .quantity = resting_quantity,
                                  .aggressive_side = S};

                event_writer.write_event(trade);
            } else {
                event_writer.write_event(OrderCancelled{.id = resting_id});
            }
        } else {
            level.total_quantity -= remaining;
            node.quantity -= remaining;
            if (emit_trade) {
                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = node.id,
                                  .price = level_price,
                                  .quantity = remaining,
                                  .aggressive_side = S};

                event_writer.write_event(trade);
            } else {
                event_writer.write_event(
                    OrderReduced{.id = node.id, .new_quantity = node.quantity});
            }
            remaining = Quantity{0};
        }
    }

    if (remaining == Quantity{0})
        return MatchOutcome::Filled;
    else
        return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S, bool StpActive>
MatchOutcome
DenseLadderOrderBook<BandWidth, Hash>::match_order(Quantity &remaining, const NewOrder &order,
                                                   EventWriter &event_writer) noexcept {
    Price limit = effective_limit(order);

    constexpr Side RestingSide = S == Side::Buy ? Side::Sell : Side::Buy;

    MatchOutcome outcome = MatchOutcome::Exhausted;
    auto visit = [&](Level &level, Price price) {
        outcome = match_level<S, StpActive>(level, remaining, price, order, event_writer);
        return outcome == MatchOutcome::Exhausted ? WalkControl::Continue : WalkControl::Stop;
    };

    this->template walk_side<RestingSide>(limit, visit);

    return outcome;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
AddStatus
DenseLadderOrderBook<BandWidth, Hash>::validate_new_order(const NewOrder &order,
                                                          bool check_duplicate) const noexcept {
    if (check_duplicate) {
        std::size_t slot = this->find_order_location(order.id);
        if (slot != invalid_hash_slot) {
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
            order.time_in_force != TimeInForce::Ioc) {
            const AddResult result = order.side == Side::Buy
                                         ? this->template rest_order<Side::Buy>(remaining, order)
                                         : this->template rest_order<Side::Sell>(remaining, order);
            if (result.status == AddStatus::Rested)
                event_writer.write_event(OrderRested{.id = order.id,
                                                     .price = order.price,
                                                     .quantity = remaining,
                                                     .side = order.side,
                                                     .stp_id = order.stp_id,
                                                     .time_in_force = order.time_in_force});
            return result;
        }
        [[fallthrough]];
    case MatchOutcome::Aborted:
        return AddResult{.remaining = remaining,
                         .status = AddStatus::RemainderCancelled,
                         .outcome = match_outcome};
    }

    std::unreachable();
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

    MatchOutcome outcome = MatchOutcome::Exhausted;
    auto visit = [&](const Level &level, Price price) {
        outcome = scan_level<StpActive>(level, price, order, remaining);
        return outcome == MatchOutcome::Exhausted ? WalkControl::Continue : WalkControl::Stop;
    };

    this->template walk_side<S>(limit, visit);

    return outcome == MatchOutcome::Filled;
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