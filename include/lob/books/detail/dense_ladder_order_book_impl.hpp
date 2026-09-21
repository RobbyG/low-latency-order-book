// Included by <lob/books/dense_ladder_order_book.hpp>. Do not include directly.

#pragma once

#include <lob/books/dense_ladder_order_book.hpp>
#include <lob/trade.hpp>
#include <lob/trade_writer.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>

namespace lob::books {

// construction
template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
DenseLadderOrderBook<BandWidth, Hash>::DenseLadderOrderBook(Config config) : config_(config) {
    reserve(config_);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
AddResult DenseLadderOrderBook<BandWidth, Hash>::add_order(const NewOrder &order,
                                                           TradeWriter &trade_writer) {
    assert(order.quantity != Quantity{0} && "Order cannot have a quantity of 0");
    // validate
    AddStatus status = validate_new_order(order);
    if (status != AddStatus::Accepted) {
        return AddResult{.remaining = order.quantity,
                         .trade_count = 0,
                         .status = status,
                         .outcome = MatchOutcome::None};
    }
    // match
    if (order.side == Side::Buy)
        result = order.stp_id != StpId{0} ? match_order<Side::Buy, true>(order, trade_writer)
                                          : match_order<Side::Buy, false>(order, trade_writer);
    else
        result = order.stp_id != StpId{0} ? match_order<Side::Buy, true>(order, trade_writer)
                                          : match_order<Side::Buy, false>(order, trade_writer);

    // rest if required
    AddResult result;
    return result;
}

// private functions
template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::reserve(const Config &config) {

    base_price_ = config.base;

    resting_order_pool_.reserve(config.max_orders);

    if (config.max_orders == 0)
        resting_order_pool_head_ = invalid_index;
    else {
        resting_order_pool_head_ = 0;

        for (std::uint32_t i = 0; i < config.max_orders - 1; ++i)
            resting_order_pool_[i].next = i + 1;

        resting_order_pool_[config.max_orders - 1].next = invalid_index;
    }

    std::size_t hash_slots =
        std::max<std::size_t>(2, static_cast<std::size_t>(config.max_orders * 2));
    hash_slots = std::bit_ceil(hash_slots);

    order_index_shift_ = 64 - std::countr_zero(hash_slots);
    order_index_.resize(hash_slots);
    order_index_mask_ = hash_slots - 1;

    assert(std::has_single_bit(order_index_.size()) &&
           "The hash map (vector) must have a power of 2 size.");
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::remove_from_order_index(OrderId id) noexcept {

    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_index)
        return false;

    order_index_[slot].node_index = invalid_index;

    return true;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::remove_resting_order(
    Level &level, RestingOrderNode &node, std::uint32_t node_index) noexcept {

    level.total_quantity -= node.quantity;
    if (node.prev == invalid_index) {
        level.head = node.next;
        if (node.next != invalid_index)
            resting_order_pool_[node.next].prev = node.prev;
        else
            level.tail = invalid_index;
    } else if (node.next == invalid_index) {
        resting_order_pool_[node.prev].next = invalid_index;
        level.tail = node.prev;
    } else {
        resting_order_pool_[node.prev].next = node.next;
        resting_order_pool_[node.next].prev = node.prev;
    }

    remove_from_order_index(node.id);
    node.next = resting_order_pool_head_;
    resting_order_pool_head_ = node_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
AddStatus
DenseLadderOrderBook<BandWidth, Hash>::validate_new_order(const NewOrder &order) const noexcept {
    std::size_t slot = find_id_entry(order.id);
    if (slot != invalid_index) {
        return AddStatus::DuplicateOrderId;
    }

    if (order.time_in_force == TimeInForce::Fok && !can_fully_fill(order)) {
        return AddStatus::WouldNotFullyFill;
    }

    return AddStatus::Accepted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_level(Level &level, Quantity &remaining,
                                                        Price level_price, const NewOrder &order,
                                                        TradeWriter &trade_writer,
                                                        std::uint32_t &trade_count)
    -> MatchOutcome {

    bool emit_trade;
    if (level.head == invalid_index)
        return MatchOutcome::Exhausted;

    std::uint32_t node_index = level.head;
    while (node_index != invalid_index && remaining > Quantity{0}) {
        emit_trade = true;
        std::uint32_t node_index_copy = node_index;
        RestingOrderNode &node = resting_order_pool_[node_index];

        if constexpr (StpActive) {
            if (order.stp_id == node.stp_id) {
                switch (order.self_trade_resolve) {
                case SelfTradeResolve::CancelBoth:
                    remove_resting_order(level, node, node_index);
                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelNew:
                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelResting:
                    node_index = node.next;
                    remove_resting_order(level, node, node_index_copy);
                    continue;
                case SelfTradeResolve::DecrementAndCancel:
                    emit_trade = false;
                    break;
                }
            }
        }

        node_index = node.next;

        if (remaining >= node.quantity) {
            remaining -= node.quantity;
            remove_resting_order(
                level, node,
                node_index_copy); // includes removing the quantity from the level.total_quantity
            if (emit_trade) {
                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = node.id,
                                  .price = level_price,
                                  .quantity = node.quantity,
                                  .aggressive_side = AggressiveSide};

                trade_writer.on_trade(trade);
                ++trade_count;
            }
        } else {
            level.total_quantity -= remaining;
            if (emit_trade) {
                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = node.id,
                                  .price = level_price,
                                  .quantity = remaining,
                                  .aggressive_side = AggressiveSide};

                trade_writer.on_trade(trade);
                ++trade_count;
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

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide, typename LevelsType>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::walk_overflow(LevelsType &levels, Price limit,
                                                                  auto &&visit) {
    constexpr bool mutating = !std::is_const_v<LevelsType>;

    for (auto it = levels.begin(); it != levels.end();) {

        auto &[price, level] = *it;
        if (worse<RestingSide>(price, limit))
            break;

        const MatchOutcome outcome = visit(level, price);

        if constexpr (mutating) {
            if (level.head == invalid_index)
                it = levels.erase(it);
            else
                ++it;
        } else {
            ++it;
        }

        if (outcome != MatchOutcome::Exhausted)
            return outcome;
    }

    return MatchOutcome::Exhausted;
}

template <Side RestingSide, typename LevelsType>

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_dense(Quantity &remaining, const NewOrder &order,
                                                        TradeWriter &trade_writer,
                                                        std::uint32_t &trade_count)
    -> MatchOutcome {

    const Price order_price = order.price;
    const Price base_price_local = base_price_;

    if constexpr (AggressiveSide == Side::Buy) {
        if (order_price < base_price_local)
            return MatchOutcome::Exhausted;

        std::size_t slot = best_ask_slot_;
        const std::size_t limit = order.order_type == OrderType::Limit
                                      ? price_diff_to_size_t(order_price, base_price_local)
                                      : BandWidth - 1;

        while (slot != invalid_index && slot <= limit) {
            const MatchOutcome result = match_level<AggressiveSide, StpActive>(
                asks_[slot], remaining, base_price_local + Price{static_cast<std::int64_t>(slot)},
                order, trade_writer, trade_count);

            if (asks_[slot].head == invalid_index) {
                asks_occupied_[slot >> 6] &=
                    ~(std::uint64_t{1} << (slot & 63)); // clear slot bit in asks_occupied_

                const std::size_t next = next_occupied_slot(asks_occupied_, slot);

                if (slot == best_ask_slot_)
                    best_ask_slot_ = next;

                slot = next;

                if (result != MatchOutcome::Exhausted)
                    return result;
            } else {
                if (result != MatchOutcome::Exhausted)
                    return result;

                slot = next_occupied_slot(asks_occupied_, slot);
            }
        }

    } else {
        std::size_t slot = best_bid_slot_;

        const std::size_t limit =
            order_price < base_price_local || order.order_type == OrderType::Limit
                ? 0
                : price_diff_to_size_t(order_price, base_price_local);

        while (slot != invalid_index && slot >= limit) {
            const MatchOutcome result = match_level<AggressiveSide, StpActive>(
                bids_[slot], remaining, base_price_local + Price{static_cast<std::int64_t>(slot)},
                order, trade_writer, trade_count);

            if (bids_[slot].head == invalid_index) {
                bids_occupied_[slot >> 6] &=
                    ~(std::uint64_t{1} << (slot & 63)); // clear slot bit in bids_occupied_

                const std::size_t previous = previous_occupied_slot(bids_occupied_, slot);

                if (slot == best_bid_slot_)
                    best_bid_slot_ = previous;

                slot = previous;

                if (result != MatchOutcome::Exhausted)
                    return result;
            } else {
                if (result != MatchOutcome::Exhausted)
                    return result;

                slot = previous_occupied_slot(bids_occupied_, slot);
            }
        }
    }

    return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::match_order(Quantity &remaining,
                                                                const NewOrder &order,
                                                                TradeWriter &trade_writer,
                                                                std::uint32_t &trade_count) {

    MatchOutcome result = match_better_overflow<AggressiveSide, StpActive>(
        remaining, order, trade_writer, trade_count);
    if (result != MatchOutcome::Exhausted)
        return result;

    result = match_dense<AggressiveSide, StpActive>(remaining, order, trade_writer, trade_count);
    if (result != MatchOutcome::Exhausted)
        return result;

    result = match_worse_overflow<AggressiveSide, StpActive>(remaining, order, trade_writer,
                                                             trade_count);
    return result;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
bool DenseLadderOrderBook<BandWidth, Hash>::rest_order(const NewOrder &order) {

    // needs to be rested
    if constexpr (AggressiveSide == Side::Buy) {

        std::uint32_t new_node_index = resting_order_pool_head_;

        std::size_t slot = probe_slot(order.id);
        if (order_index_[slot].id == order.id) {
            return AddResult{.remaining = remaining,
                             .trade_count = trade_count,
                             .status = AddStatus::DuplicateOrderId,
                             .outcome = MatchOutcome::Exhausted};
        } else {
            order_index_[slot] = IdEntry{.id = order.id,
                                         .price = order.price,
                                         .node_index = new_node_index,
                                         .side = Side::Buy};
        }

        resting_order_pool_head_ = resting_order_pool_[new_node_index].next;

        resting_order_pool_[new_node_index].id = order.id;
        resting_order_pool_[new_node_index].quantity = remaining;
        resting_order_pool_[new_node_index].stp_id = order.stp_id;
        resting_order_pool_[new_node_index].time_in_force = order.time_in_force;
        resting_order_pool_[new_node_index].next = invalid_index;

        if (order.price > base_price_ + Price{BandWidth - 1}) {
            auto it = std::lower_bound(
                bids_better_overflow_.begin(), bids_better_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price; });

            if (it != bids_better_overflow_.end() && it->first == order.price) {
                Level &level = it->second;
                resting_order_pool_[level.tail].next = new_node_index;
                resting_order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            } else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;
                level.head = new_node_index;
                level.tail = new_node_index;

                resting_order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += remaining;
            }

        } else if (order.price >= base_price_) {
            Level &level = bids_[price_diff_to_size_t(order.price, base_price_)];
            if (level.total_quantity == Quantity{0}) {
                level.head = new_node_index;
                level.tail = new_node_index;
                resting_order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += remaining;
            } else {
                resting_order_pool_[level.tail].next = new_node_index;
                resting_order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            }

        } else {

            auto it = std::lower_bound(
                bids_worse_overflow_.begin(), bids_worse_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price; });

            if (it != bids_worse_overflow_.end() && it->first == order.price) {
                Level &level = it->second;
                resting_order_pool_[level.tail].next = new_node_index;
                resting_order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            } else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;

                level.head = new_node_index;
                level.tail = new_node_index;
                resting_order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += remaining;
            }
        }

    } else {

        std::uint32_t new_node_index = resting_order_pool_head_;

        std::size_t slot = probe_slot(order.id);
        if (order_index_[slot].id == order.id) {
            return AddResult{.remaining = remaining,
                             .trade_count = trade_count,
                             .status = AddStatus::DuplicateOrderId,
                             .outcome = MatchOutcome::Exhausted};
        } else {
            order_index_[slot] = IdEntry{.id = order.id,
                                         .price = order.price,
                                         .node_index = new_node_index,
                                         .side = Side::Buy};
        }

        resting_order_pool_head_ = resting_order_pool_[new_node_index].next;

        resting_order_pool_[new_node_index].id = order.id;
        resting_order_pool_[new_node_index].quantity = remaining;
        resting_order_pool_[new_node_index].stp_id = order.stp_id;
        resting_order_pool_[new_node_index].time_in_force = order.time_in_force;
        resting_order_pool_[new_node_index].next = invalid_index;

        if (order.price > base_price_ + Price{BandWidth - 1}) {
            auto it = std::lower_bound(
                asks_better_overflow_.begin(), asks_better_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price; });

            if (it != asks_better_overflow_.end() && it->first == order.price) {
                Level &level = it->second;
                resting_order_pool_[level.tail].next = new_node_index;
                resting_order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            } else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;
                level.head = new_node_index;
                level.tail = new_node_index;

                resting_order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += remaining;
            }

        } else if (order.price >= base_price_) {
            Level &level = bids_[order.price - base_price_];
            if (level.total_quantity == 0) {
                level.head = new_node_index;
                level.tail = new_node_index;
                resting_order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += remaining;
            } else {
                resting_order_pool_[level.tail].next = new_node_index;
                resting_order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            }

        } else {

            auto it = std::lower_bound(
                asks_worse_overflow_.begin(), asks_worse_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price; });

            if (it != asks_worse_overflow_.end() && it->first == order.price) {
                Level &level = it->second;
                resting_order_pool_[level.tail].next = new_node_index;
                resting_order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            } else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;

                level.head = new_node_index;
                level.tail = new_node_index;
                resting_order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += remaining;
            }
        }
    }

    return AddResult{.remaining = remaining,
                     .trade_count = trade_count,
                     .status = AddStatus::Rested,
                     .outcome = MatchOutcome::Exhausted};
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
std::size_t DenseLadderOrderBook<BandWidth, Hash>::probe_slot(OrderId id) const noexcept {
    std::size_t slot =
        Hash::hash_into_slot(id, order_index_mask_, static_cast<std::uint32_t>(order_index_shift_));
    [[maybe_unused]] std::size_t probes = 0;

    while (true) {
        const IdEntry &entry = order_index_[slot];

        if (entry.node_index == invalid_index || entry.id == id)
            return slot;

        slot = (slot + 1) & order_index_mask_;
        assert(++probes < order_index_.size() && "table full");
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
std::size_t DenseLadderOrderBook<BandWidth, Hash>::find_id_entry(OrderId id) const noexcept {
    const std::size_t slot = probe_slot(id);
    return order_index_[slot].node_index == invalid_index ? invalid_index : slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
std::size_t
DenseLadderOrderBook<BandWidth, Hash>::previous_occupied_slot(const auto &occupied,
                                                              std::size_t slot) const noexcept {
    if (slot == 0)
        return invalid_index;

    std::size_t word_index = slot >> 6; // /64 keep first bits, except the last 6
    const unsigned bit_index = static_cast<unsigned>(slot & 63); // the last 6 bits only

    std::uint64_t word = occupied[word_index] & ((std::uint64_t{1} << bit_index) - 1);

    if (word != 0) {
        return (word_index << 6) + (63u - std::countl_zero(word));
    }

    while (word_index != 0) {
        word = occupied[--word_index];

        if (word != 0) {
            return (word_index << 6) + (63u - std::countl_zero(word));
        }
    }

    return invalid_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
std::size_t
DenseLadderOrderBook<BandWidth, Hash>::next_occupied_slot(const auto &occupied,
                                                          std::size_t slot) const noexcept {
    if (slot == BandWidth - 1)
        return invalid_index;

    std::size_t word_index = slot >> 6;
    const unsigned bit_index = static_cast<unsigned>(slot & 63);

    std::uint64_t word = occupied[word_index] & ((~std::uint64_t{0} << bit_index) << 1);

    if (word != 0) {
        return (word_index << 6) + std::countr_zero(word);
    }

    while (++word_index < occupied.size()) {
        word = occupied[word_index];

        if (word != 0) {
            return (word_index << 6) + std::countr_zero(word);
        }
    }

    return invalid_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <bool ExcludeOrder, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::scan_level(const Level &level, Price level_price,
                                                       const NewOrder &order, Quantity &remaining,
                                                       const ExcludedOrder &excluded) const noexcept
    -> ScanOutcome {

    if constexpr (StpActive) {
        const StpId stp_id = order.stp_id;
        const SelfTradeResolve stp_policy = order.self_trade_resolve;

        std::uint32_t node = level.head;

        while (node != invalid_index) {
            const RestingOrderNode &resting = resting_order_pool_[node];
            const std::uint32_t next = resting.next;

            if constexpr (ExcludeOrder) {
                if (node == excluded.node_index) {
                    node = next;
                    continue;
                }
            }

            if (resting.stp_id == stp_id) {
                switch (stp_policy) {
                case SelfTradeResolve::CancelNew:
                case SelfTradeResolve::CancelBoth:
                    return ScanOutcome::WillAbort;

                case SelfTradeResolve::CancelResting:
                    node = next;
                    continue;

                case SelfTradeResolve::DecrementAndCancel:
                    break;
                }
            }

            if (resting.quantity >= remaining)
                return ScanOutcome::WillFill;

            remaining -= resting.quantity;
            node = next;
        }

    } else {
        Quantity available = level.total_quantity;

        if constexpr (ExcludeOrder) {
            if (level_price == excluded.price)
                available -= excluded.quantity;
        }

        if (available >= remaining)
            return ScanOutcome::WillFill;

        remaining -= available;
    }

    return ScanOutcome::WillExhaust;
}

template <std::size_t Bandwidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder, bool StpActive>
auto DenseLadderOrderBook<Bandwidth, Hash>::scan_dense(const NewOrder &order, Quantity &remaining,
                                                       const ExcludedOrder &excluded) const noexcept
    -> ScanOutcome {

    const Price base_price_local = base_price_;
    const Price new_order_price = order.price;

    if constexpr (OppositeSide == Side::Buy) {
        std::size_t slot = best_bid_slot_;
        // if new_order_price > base_price + BandWidth, then the new order price is out of band
        // and the loop will not run
        const std::size_t limit_slot = new_order_price >= base_price_local
                                           ? price_diff_to_size_t(new_order_price, base_price_local)
                                           : 0;

        while (slot != invalid_index && slot >= limit_slot) {
            const ScanOutcome result = scan_level<ExcludeOrder, StpActive>(
                bids_[slot], Price{static_cast<std::int64_t>(slot)} + base_price_local, order,
                remaining, excluded);
            if (result != ScanOutcome::WillExhaust)
                return result;
            slot = previous_occupied_slot(bids_occupied_, slot);
        }
    } else {
        std::size_t slot = best_ask_slot_;
        // if new_order_price < base_price, then the new order price is out of band and the loop
        // will not run
        if (new_order_price < base_price_local)
            return ScanOutcome::WillExhaust;

        const std::size_t limit_slot = price_diff_to_size_t(new_order_price, base_price_local);

        while (slot != invalid_index && slot <= limit_slot) {
            const ScanOutcome result = scan_level<ExcludeOrder, StpActive>(
                asks_[slot], Price{static_cast<std::int64_t>(slot)} + base_price_local, order,
                remaining, excluded);
            if (result != ScanOutcome::WillExhaust)
                return result;
            slot = next_occupied_slot(asks_occupied_, slot);
        }
    }
    return ScanOutcome::WillExhaust;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::scan_worse_overflow(
    const NewOrder &order, Quantity &remaining, const ExcludedOrder &excluded) const noexcept
    -> ScanOutcome {

    if constexpr (OppositeSide == Side::Buy) {
        for (const auto &[price, level] : bids_worse_overflow_) {
            if (price < order.price)
                break;
            const ScanOutcome result =
                scan_level<ExcludeOrder, StpActive>(level, price, order, remaining, excluded);
            if (result != ScanOutcome::WillExhaust)
                return result;
        }
    } else {

        for (const auto &[price, level] : asks_worse_overflow_) {
            if (price > order.price)
                break;
            const ScanOutcome result =
                scan_level<ExcludeOrder, StpActive>(level, price, order, remaining, excluded);
            if (result != ScanOutcome::WillExhaust)
                return result;
        }
    }

    return ScanOutcome::WillExhaust;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool StpActive>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(const NewOrder &order) const noexcept {
    return can_fill_levels<OppositeSide, false, StpActive>(order, 0);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder, bool StpActive>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(
    const NewOrder &order, std::size_t excluded_slot) const noexcept {
    Quantity remaining = order.quantity;
    ExcludedOrder excluded{.node_index = invalid_index, .price = Price{}, .quantity = Quantity{}};

    if constexpr (ExcludeOrder) {
        assert(excluded_slot < order_index_.size() &&
               "excluded_slot must be a valid index into order_index_");
        const IdEntry &entry = order_index_[excluded_slot];

        if (entry.side == OppositeSide) {
            excluded.node_index = entry.node_index;
            excluded.price = entry.price;
            excluded.quantity = resting_order_pool_[entry.node_index].quantity;
        }
        // if we have the excluded order on the wrong side, just mark it with node index
        // invalid_index and quantity 0
    }

    ScanOutcome result =
        scan_better_overflow<OppositeSide, ExcludeOrder, StpActive>(order, remaining, excluded);
    if (result == ScanOutcome::WillAbort)
        return false;
    if (result == ScanOutcome::WillFill)
        return true;

    result = scan_dense<OppositeSide, ExcludeOrder, StpActive>(order, remaining, excluded);
    if (result == ScanOutcome::WillAbort)
        return false;
    if (result == ScanOutcome::WillFill)
        return true;

    result = scan_worse_overflow<OppositeSide, ExcludeOrder, StpActive>(order, remaining, excluded);
    if (result == ScanOutcome::WillAbort)
        return false;
    if (result == ScanOutcome::WillFill)
        return true;

    return false;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fully_fill(const NewOrder &order) const noexcept {
    if (order.side == Side::Buy)
        return order.stp_id != StpId{0} ? can_fill_levels<Side::Sell, true>(order)
                                        : can_fill_levels<Side::Sell, false>(order);
    else
        return order.stp_id != StpId{0} ? can_fill_levels<Side::Buy, true>(order)
                                        : can_fill_levels<Side::Buy, false>(order);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fully_fill(
    const NewOrder &order, std::size_t excluded_slot) const noexcept {

    if (order.side == Side::Buy)
        return order.stp_id != StpId{0}
                   ? can_fill_levels<Side::Sell, true, true>(order, excluded_slot)
                   : can_fill_levels<Side::Sell, true, false>(order, excluded_slot);
    else
        return order.stp_id != StpId{0}
                   ? can_fill_levels<Side::Buy, true, true>(order, excluded_slot)
                   : can_fill_levels<Side::Buy, true, false>(order, excluded_slot);
}

} // namespace lob::books