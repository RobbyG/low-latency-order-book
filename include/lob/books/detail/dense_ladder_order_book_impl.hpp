// Included by <lob/books/dense_ladder_order_book.hpp>. Do not include directly.

#pragma once

#include <lob/books/dense_ladder_order_book.hpp>

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

    AddStatus status = validate_new_order(order);
    if (status != AddStatus::Accepted) {
        return AddResult{.remaining = order.quantity,
                         .trade_count = 0,
                         .status = status,
                         .outcome = MatchOutcome::None};
    }

    return add_validated_order(order, trade_writer);
}

// private functions
template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::reserve(const Config &config) {
    order_pool_.reserve(config.max_orders);

    base_price_ = config.base;

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
            order_pool_[node.next].prev = node.prev;
        else
            level.tail = invalid_index;
    } else if (node.next == invalid_index) {
        order_pool_[node.prev].next = invalid_index;
        level.tail = node.prev;
    } else {
        order_pool_[node.prev].next = node.next;
        order_pool_[node.next].prev = node.prev;
    }

    remove_from_order_index(node.id);
    node.next = resting_pool_head_;
    resting_pool_head_ = node_index;
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
AddResult DenseLadderOrderBook<BandWidth, Hash>::add_validated_order(const NewOrder &order,
                                                                     TradeWriter &trade_writer) {
    if (order.side == Side::Buy)
        return match_and_add<Side::Buy>(order, trade_writer);
    else
        return match_and_add<Side::Sell>(order, trade_writer);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side SameSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_level(Level &level, Quantity &remaining,
                                                        Price level_price, NewOrder &order,
                                                        TradeWriter &trade_writer,
                                                        std::uint32_t &trade_count)
    -> MatchOutcome {

    bool emit_trade = true;
    if (level.head == invalid_index)
        return MatchOutcome::Exhausted;

    std::uint32_t node_index = level.head;
    while (node_index != invalid_index && remaining > 0) {
        emit_trade = true;
        std::uint32_t node_index_copy = node_index;
        RestingOrderNode &node = order_pool_[node_index];
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
                                  .aggresside_side = SameSide};

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
                                  .aggresside_side = SameSide};

                trade_writer.on_trade(trade);
                ++trade_count;
            }
            node.quantity -= remaining;
            remaining = 0;
        }
    }

    if (remaining == 0)
        return MatchOutcome::Filled;
    else
        return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side SameSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_better_overflow(Quantity &remaining,
                                                                  NewOrder &order,
                                                                  TradeWriter &trade_writer,
                                                                  std::uint32_t &trade_count)
    -> MatchOutcome {

    const Price order_price = order.price;

    if constexpr (SameSide == Side::Buy) {
        for (auto it = asks_better_overflow_.begin(); it != asks_better_overflow_.end();) {

            auto &[price, level] = *it;

            if (order.order_type == OrderType::Limit && order_price < price)
                break;

            const MatchOutcome result = match_level<SameSide, StpActive>(
                level, remaining, price, order, trade_writer, trade_count);

            if (level.head == invalid_index)
                it = asks_better_overflow_.erase(it);
            else
                ++it;

            if (result != MatchOutcome::Exhausted)
                return result;
        }

    } else {
        for (auto it = bids_better_overflow_.begin(); it != bids_better_overflow_.end();) {

            auto &[price, level] = *it;

            if (order.order_type == OrderType::Limit && order_price > price)
                break;

            const MatchOutcome result = match_level<SameSide, StpActive>(
                level, remaining, price, order, trade_writer, trade_count);

            if (level.head == invalid_index)
                it = bids_better_overflow_.erase(it);
            else
                ++it;

            if (result != MatchOutcome::Exhausted)
                return result;
        }
    }

    return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side SameSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_dense(Quantity &remaining, NewOrder &order,
                                                        TradeWriter &trade_writer,
                                                        std::uint32_t &trade_count)
    -> MatchOutcome {

    const Price order_price = order.price;
    const Price base_price_local = base_price_;

    if constexpr (SameSide == Side::Buy) {
        if (order_price < base_price_local)
            return MatchOutcome::Exhausted;

        std::size_t slot = best_ask_slot_;
        const std::size_t limit = order.order_type == OrderType::Limit
                                      ? static_cast<std::size_t>(order_price - base_price_local)
                                      : base_price_local + BandWidth - 1;

        while (slot != invalid_index && slot <= limit) {
            const MatchOutcome result = match_level<SameSide, StpActive>(
                asks_[slot], remaining, base_price_local + slot, order, trade_writer, trade_count);

            if (asks_[slot].head == invalid_index) {
                asks_occupied_[slot >> 6] &= ~(std::uint64_t{1} << (slot & 63));

                const std::size_t next = next_occupied_slot(asks_occupied_, slot);

                if (slot == best_ask_slot_)
                    best_ask_slot_ = next;

                slot = next;
            } else {
                if (result != MatchOutcome::Exhausted)
                    return result;

                slot = next_occupied_slot(asks_occupied_, slot);
            }

            if (result != MatchOutcome::Exhausted)
                return result;
        }

    } else {
        std::size_t slot = best_bid_slot_;

        const std::size_t limit =
            order_price < base_price_local || order.order_type == OrderType::Market
                ? 0
                : static_cast<std::size_t>(order_price - base_price_local);

        while (slot != invalid_index && slot >= limit) {
            const MatchOutcome result = match_level<SameSide, StpActive>(
                bids_[slot], remaining, base_price_local + slot, order, trade_writer, trade_count);

            if (bids_[slot].head == invalid_index) {
                bids_occupied_[slot >> 6] &= ~(std::uint64_t{1} << (slot & 63));

                const std::size_t previous = previous_occupied_slot(bids_occupied_, slot);

                if (slot == best_bid_slot_)
                    best_bid_slot_ = previous;

                slot = previous;
            } else {
                if (result != MatchOutcome::Exhausted)
                    return result;

                slot = previous_occupied_slot(bids_occupied_, slot);
            }

            if (result != MatchOutcome::Exhausted)
                return result;
        }
    }

    return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side SameSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_worse_overflow(Quantity &remaining,
                                                                 NewOrder &order,
                                                                 TradeWriter &trade_writer,
                                                                 std::uint32_t &trade_count)
    -> MatchOutcome {

    const Price order_price = order.price;

    if constexpr (SameSide == Side::Buy) {
        for (auto it = asks_worse_overflow_.begin(); it != asks_worse_overflow_.end();) {
            auto &[price, level] = *it;

            if (order.order_type == OrderType::Limit && order_price < price)
                break;

            const MatchOutcome result = match_level<SameSide, StpActive>(
                level, remaining, price, order, trade_writer, trade_count);

            if (level.head == invalid_index)
                it = asks_worse_overflow_.erase(it);
            else
                ++it;

            if (result != MatchOutcome::Exhausted)
                return result;
        }

    } else {
        for (auto it = bids_worse_overflow_.begin(); it != bids_worse_overflow_.end();) {
            auto &[price, level] = *it;

            if (order.order_type == OrderType::Limit && order_price > price)
                break;

            const MatchOutcome result = match_level<SameSide, StpActive>(
                level, remaining, price, order, trade_writer, trade_count);

            if (level.head == invalid_index)
                it = bids_worse_overflow_.erase(it);
            else
                ++it;

            if (result != MatchOutcome::Exhausted)
                return result;
        }
    }

    return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side SameSide>
AddResult DenseLadderOrderBook<BandWidth, Hash>::match_and_add(NewOrder &order,
                                                               TradeWriter &trade_writer) {
    Quantity remaining = order.quantity;
    std::uint32_t trade_count = 0;
    const bool stp_active = order.stp_id == 0 ? false : true;

    if (resting_pool_head_ == invalid_index) {
        return AddResult{.remaining = remaining,
                         .trade_count = trade_count,
                         .status = AddStatus::BookFull,
                         .outcome = MatchOutcome::None};
    }

    if (order.time_in_force == TimeInForce::Fok) {
        if (!can_fully_fill(order)) {
            return AddResult{.remaining = remaining,
                             .trade_count = trade_count,
                             .status = AddStatus::WouldNotFullyFill,
                             .outcome = MatchOutcome::None};
        }
    }

    MatchOutcome result =
        stp_active
            ? match_better_overflow<SameSide, true>(remaining, order, trade_writer, trade_count)
            : match_better_overflow<SameSide, false>(remaining, order, trade_writer, trade_count);
    if (result != MatchOutcome::Exhausted)
        return AddResult{.remaining = remaining,
                         .trade_count = trade_count,
                         .status = AddStatus::Accepted,
                         .outcome = result};
    result = stp_active ? match_dense<SameSide, true>(remaining, order, trade_writer, trade_count)
                        : match_dense<SameSide, false>(remaining, order, trade_writer, trade_count);
    if (result != MatchOutcome::Exhausted)
        return AddResult{.remaining = remaining,
                         .trade_count = trade_count,
                         .status = AddStatus::Accepted,
                         .outcome = result};
    result =
        stp_active
            ? match_worse_overflow<SameSide, true>(remaining, order, trade_writer, trade_count)
            : match_worse_overflow<SameSide, false>(remaining, order, trade_writer, trade_count);
    if (result != MatchOutcome::Exhausted)
        return AddResult{.remaining = remaining,
                         .trade_count = trade_count,
                         .status = AddStatus::Accepted,
                         .outcome = result};

    if (order.time_in_force == TimeInForce::Ioc) {
        return AddResult{.remaining = remaining,
                         .trade_count = trade_count,
                         .status = AddStatus::Accepted,
                         .outcome = MatchOutcome::RemainderCancelled};
    }

    if constexpr (SameSide == Side::Buy) {

        std::uint32_t new_node_index = resting_pool_head_;

        std::size_t slot = probe_slot(order.id);
        if (order_index_[slot].id == order.id) {
            return return AddResult{.remaining = remaining,
                                    .trade_count = trade_count,
                                    .status = AddStatus::DuplicateOrderId,
                                    .outcome = MatchOutcome::RemainderCancelled};
        } else {
            order_index_[slot] = IdEntry {
                .id = order.id, .price = order.price, .node_index = new_node_index,
                .side = Side::Buy
            }
        }

        resting_pool_head_ = order_pool_[new_node_index].next;

        order_pool_[new_node_index].id = order.id;
        order_pool_[new_node_index].quantity = remaining;
        order_pool_[new_node_index].stp_id = order.stp_id;
        order_pool_[new_node_index].time_in_force = order.time_in_force;
        order_pool_[new_node_index].next = invalid_index;

        if (order.price > base_price_ + BandWidth - 1) {
            auto it = std::lower_bound(
                bids_better_overflow_.begin(), bids_better_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price });

            if (it != overflow.end() && it->first == order.price)
                Level &level = it->second;
            order_pool_[level.tail].next = new_node_index;
            order_pool_[new_node_index].prev = level.tail;
            level.tail = new_node_index;
            level.total_quantity += remaining;

            else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;
                level.head = new_node_index;
                level.tail = new_node_index;

                order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += ramaining;
            }

        } else if (order.price >= base_price_) {
            Level &level = bids_[order.price - base_price];
            if (level.total_quantity == 0) {
                level.head = new_node_index;
                level.tail = new_node_index;
                order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += ramaining;
            } else {
                order_pool_[level.tail].next = new_node_index;
                order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            }

        } else {

            auto it = std::lower_bound(
                bids_worse_overflow_.begin(), bids_worse_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price });

            if (it != overflow.end() && it->first == order.price)
                Level &level = it->second;
            order_pool_[level.tail].next = new_node_index;
            order_pool_[new_node_index].prev = level.tail;
            level.tail = new_node_index;
            level.total_quantity += remaining;

            else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;

                level.head = new_node_index;
                level.tail = new_node_index;
                order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += ramaining;
            }
        }

    } else {

        std::uint32_t new_node_index = resting_pool_head_;

        std::size_t slot = probe_slot(order.id);
        if (order_index_[slot].id == order.id) {
            return return AddResult{.remaining = remaining,
                                    .trade_count = trade_count,
                                    .status = AddStatus::DuplicateOrderId,
                                    .outcome = MatchOutcome::RemainderCancelled};
        } else {
            order_index_[slot] = IdEntry {
                .id = order.id, .price = order.price, .node_index = new_node_index,
                .side = Side::Buy
            }
        }

        resting_pool_head_ = order_pool_[new_node_index].next;

        order_pool_[new_node_index].id = order.id;
        order_pool_[new_node_index].quantity = remaining;
        order_pool_[new_node_index].stp_id = order.stp_id;
        order_pool_[new_node_index].time_in_force = order.time_in_force;
        order_pool_[new_node_index].next = invalid_index;

        if (order.price > base_price_ + BandWidth - 1) {
            auto it = std::lower_bound(
                asks_better_overflow_.begin(), asks_better_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price });

            if (it != overflow.end() && it->first == order.price)
                Level &level = it->second;
            order_pool_[level.tail].next = new_node_index;
            order_pool_[new_node_index].prev = level.tail;
            level.tail = new_node_index;
            level.total_quantity += remaining;

            else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;
                level.head = new_node_index;
                level.tail = new_node_index;

                order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += ramaining;
            }

        } else if (order.price >= base_price_) {
            Level &level = bids_[order.price - base_price];
            if (level.total_quantity == 0) {
                level.head = new_node_index;
                level.tail = new_node_index;
                order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += ramaining;
            } else {
                order_pool_[level.tail].next = new_node_index;
                order_pool_[new_node_index].prev = level.tail;
                level.tail = new_node_index;
                level.total_quantity += remaining;
            }

        } else {

            auto it = std::lower_bound(
                asks_worse_overflow_.begin(), asks_worse_overflow_.end(), order.price,
                [](const auto &entry, Price price) { return entry.first > price });

            if (it != overflow.end() && it->first == order.price)
                Level &level = it->second;
            order_pool_[level.tail].next = new_node_index;
            order_pool_[new_node_index].prev = level.tail;
            level.tail = new_node_index;
            level.total_quantity += remaining;

            else {
                it = bids_better_overflow_.insert(it, {order.price, Level{}});
                Level &level = it->second;

                level.head = new_node_index;
                level.tail = new_node_index;
                order_pool_[new_node_index].prev = invalid_index;
                level.total_quantity += ramaining;
            }
        }
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
std::size_t DenseLadderOrderBook<BandWidth, Hash>::probe_slot(OrderId id) const noexcept {
    std::size_t slot = Hash::hash_into_slot(id, order_index_mask_, order_index_shift_);
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
    const std::size_t slot probe_slot(id);
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
    -> FillScan {

    if constexpr (StpActive) {
        const StpId stp_id = order.stp_id;
        const SelfTradeResolve stp_policy = order.self_trade_resolve;

        std::uint32_t node = level.head;

        while (node != invalid_index) {
            const RestingOrderNode &resting = order_pool_[node];
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
                    return FillScan::Aborted;

                case SelfTradeResolve::CancelResting:
                    node = next;
                    continue;

                case SelfTradeResolve::DecrementAndCancel:
                    break;
                }
            }

            if (resting.quantity >= remaining)
                return FillScan::Filled;

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
            return FillScan::Filled;

        remaining -= available;
    }

    return FillScan::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::scan_better_overflow(
    const NewOrder &order, Quantity &remaining, const ExcludedOrder &excluded) const noexcept
    -> FillScan {

    if constexpr (OppositeSide == Side::Buy) {
        for (const auto &[price, level] : bids_better_overflow_) {
            if (price < order.price)
                break;
            const FillScan result =
                scan_level<ExcludeOrder, StpActive>(level, price, order, remaining, excluded);
            if (result != FillScan::Exhausted)
                return result;
        }
    } else {

        for (const auto &[price, level] : asks_better_overflow_) {
            if (price > order.price)
                break;
            const FillScan result =
                scan_level<ExcludeOrder, StpActive>(level, price, order, remaining, excluded);
            if (result != FillScan::Exhausted)
                return result;
        }
    }

    return FillScan::Exhausted;
}

template <std::size_t Bandwidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder, bool StpActive>
auto DenseLadderOrderBook<Bandwidth, Hash>::scan_dense(const NewOrder &order, Quantity &remaining,
                                                       const ExcludedOrder &excluded) const noexcept
    -> FillScan {

    const base_price_local = base_price_;
    const Price new_order_price = order.price;

    if constexpr (OppositeSide == Side::Buy) {
        std::size_t slot = best_bid_slot_;
        // if new_order_price > base_price + BandWidth, then the new order price is out of band
        // and the loop will not run
        const std::size_t limit_slot =
            new_order_price >= base_price_local
                ? static_cast<std::size_t>(new_order_price - base_price_local)
                : 0;

        while (slot != invalid_index && slot >= limit_slot) {
            const FillScan result = scan_level<ExcludeOrder, StpActive>(
                bids_[slot], slot + base_price_local, order, remaining, excluded);
            if (result != FillScan::Exhausted)
                return result;
            slot = previous_occupied_slot(bids_occupied_, slot);
        }
    } else {
        std::size_t slot = best_ask_slot_;
        // if new_order_price < base_price, then the new order price is out of band and the loop
        // will not run
        if (new_order_price < base_price_local)
            return FillScan::Exhausted;

        const std::size_t limit_slot = static_cast<std::size_t>(new_order_price - base_price_local);

        while (slot != invalid_index && slot <= limit_slot) {
            const FillScan result = scan_level<ExcludeOrder, StpActive>(
                asks_[slot], slot + base_price_local, order, remaining, excluded);
            if (result != FillScan::Exhausted)
                return result;
            slot = next_occupied_slot(asks_occupied_, slot);
        }
    }
    return FillScan::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::scan_worse_overflow(
    const NewOrder &order, Quantity &remaining, const ExcludedOrder &excluded) const noexcept
    -> FillScan {

    if constexpr (OppositeSide == Side::Buy) {
        for (const auto &[price, level] : bids_worse_overflow_) {
            if (price < order.price)
                break;
            const FillScan result =
                scan_level<ExcludeOrder, StpActive>(level, price, order, remaining, excluded);
            if (result != FillScan::Exhausted)
                return result;
        }
    } else {

        for (const auto &[price, level] : asks_worse_overflow_) {
            if (price > order.price)
                break;
            const FillScan result =
                scan_level<ExcludeOrder, StpActive>(level, price, order, remaining, excluded);
            if (result != FillScan::Exhausted)
                return result;
        }
    }

    return FillScan::Exhausted;
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
            excluded.quantity = order_pool_[entry.node_index].quantity;
        }
        // if we have the excluded order on the wrong side, just mark it with node index
        // invalid_index and quantity 0
    }

    FillScan result =
        scan_better_overflow<OppositeSide, ExcludeOrder, StpActive>(order, remaining, excluded);
    if (result == FillScan::Aborted)
        return false;
    if (result == FillScan::Filled)
        return true;

    result = scan_dense<OppositeSide, ExcludeOrder, StpActive>(order, remaining, excluded);
    if (result == FillScan::Aborted)
        return false;
    if (result == FillScan::Filled)
        return true;

    result = scan_worse_overflow<OppositeSide, ExcludeOrder, StpActive>(order, remaining, excluded);
    if (result == FillScan::Aborted)
        return false;
    if (result == FillScan::Filled)
        return true;

    return false;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fully_fill(const NewOrder &order) const noexcept {
    if (order.side == Side::Buy)
        return order.stp_id != StpId{0} ? can_fill_levels<Side::Sell, false, true>(order)
                                        : can_fill_levels<Side::Sell, false, false>(order);
    else
        return order.stp_id != StpId{0} ? can_fill_levels<Side::Buy, false, true>(order)
                                        : can_fill_levels<Side::Buy, false, false>(order);
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