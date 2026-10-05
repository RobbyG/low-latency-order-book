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
    assert(order.quantity != Quantity{0} && "a new order cannot have a quantity of 0");

    AddStatus status = validate_new_order(order);
    if (status != AddStatus::Accepted) {
        return AddResult{
            .remaining = order.quantity, .status = status, .outcome = MatchOutcome::None};
    }

    return add_validated_order(order, trade_writer);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
CancelResult DenseLadderOrderBook<BandWidth, Hash>::cancel_order_at_slot(OrderId id) noexcept {
    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_index)
        return CancelResult{.quantity = Quantity{0}, .status = CancelStatus::NotFound};

    cancel_order_at_slot(slot);

    return CancelResult{.quantity = cancelled_quantity, .status = CancelStatus::Cancelled};
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
ReduceResult DenseLadderOrderBook<BandWidth, Hash>::reduce_order_by(OrderId id,
                                                                    Quantity quantity) noexcept {

    assert(quantity > 0 && "the quantity to reduce by must be greater than 0");
    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_index)
        return ReduceResult{.new_quantity = Quantity{0},
                            .old_quantity = Quantity{0},
                            .status = ReduceStatus::NotFound};

    const IdEntry entry = order_index_[slot];
    auto &order = resting_order_pool_[entry.node_index];
    const Quantity old_quantity = order.quantity;

    if (old_quantity < quantity)
        return ReduceResult{.new_quantity = old_quantity,
                            .old_quantity = old_quantity,
                            .status = ReduceStatus::ExceedsRestingQuantity};

    if (old_quantity == quantity) {
        cancel_order_at_slot(slot);
        return ReduceResult{.new_quantity = Quantity{0},
                            .old_quantity = old_quantity,
                            .status = ReduceStatus::Cancelled};
    }

    Level &level = entry.side == Side::Buy ? get_level<Side::Buy>(entry.price)
                                           : get_level<Side::Sell>(entry.price);
    const Quantity new_quantity = old_quantity - quantity;
    order.quantity = new_quantity;
    return ReduceResult{.new_quantity = new_quantity,
                        .old_quantity = old_quantity,
                        .status = ReduceStatus::Reduced};
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
ReplaceResult DenseLadderOrderBook<BandWidth, Hash>::replace_order(OrderId id,
                                                                   const NewOrder &order,
                                                                   TradeWriter &trade_writer) {
    assert(order.quantity != Quantity{0} && "a new order cannot have a quantity of 0");

    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_index)
        return ReplaceResult{.new_quantity = Quantity{0},
                             .add_status = AddStatus::NotAttempted,
                             .cancel_status = CancelStatus::NotFound,
                             .outcome = MatchOutcome::None};

    AddStatus add_status = validate_new_order(order);
    if (add_status != AddStatus::Accepted)
        return ReplaceResult{.new_quantity = Quantity{0},
                             .add_status = add_status,
                             .cancel_status = CancelStatus::NotAttempted,
                             .outcome = MatchOutcome::None};

    cancel_order_at_slot(slot);

    AddResult result = add_validated_order(order, trade_writer);

    return ReplaceResult{.new_quantity = result.remaining,
                         .add_status = result.status,
                         .cancel_status = CancelStatus::Cancelled,
                         .outcome = result.outcome};
}

// private functions
template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::reserve(const Config &config) {

    base_price_ = config.base;
    upper_price_ = config.base + Price{BandWidth - 1};

    resting_order_pool_.resize(config.max_orders);

    if (config.max_orders == 0)
        resting_order_pool_head_ = invalid_index;
    else {
        resting_order_pool_head_ = 0;

        for (std::uint32_t i = 0; i < config.max_orders - 1; ++i)
            resting_order_pool_[i].next = i + 1;

        resting_order_pool_[config.max_orders - 1].next = invalid_index;
    }

    std::size_t hash_slots =
        std::max<std::size_t>(2, static_cast<std::size_t>(config.max_orders) * 2);
    hash_slots = std::bit_ceil(hash_slots);

    order_index_shift_ = static_cast<unsigned>(64 - std::countr_zero(hash_slots));
    order_index_.resize(hash_slots);
    order_index_mask_ = hash_slots - 1;

    assert(std::has_single_bit(order_index_.size()) &&
           "The hash map (vector) must have a power of 2 size.");
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::erase_slot_from_order_index(std::size_t slot) noexcept {
    const std::size_t mask = order_index_mask_;
    order_index_[slot].node_index = invalid_index;
    std::size_t empty_slot = slot;

    slot = (slot + 1) & mask;
    while (order_index_[slot].node_index != invalid_index) {
        IdEntry &entry_to_check = order_index_[slot];
        const std::size_t home = Hash::hash_into_slot(entry_to_check.id, mask, order_index_shift_);

        if (((slot - home) & mask) >= ((slot - empty_slot) & mask)) {
            order_index_[empty_slot] = entry_to_check;
            empty_slot = slot;
        }
        slot = (slot + 1) & mask;
    }
    order_index_[empty_slot].node_index = invalid_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::cancel_order_at_slot(
    std::size_t order_index_slot) noexcept {
    const IdEntry entry = order_index_[slot];
    const Quantity cancelled_quantity = resting_order_pool_[entry.node_index].quantity;

    if (entry.side == Side::Buy)
        remove_resting_at<Side::Buy>(entry.price, entry.node_index);
    else
        remove_resting_at<Side::Sell>(entry.price, entry.node_index);

    erase_slot_from_order_index(slot);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::remove_from_order_index(OrderId id) noexcept {

    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_index)
        return false;

    erase_slot_from_order_index(slot);

    return true;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide>
auto DenseLadderOrderBook<BandWidth, Hash>::get_level(Price price) noexcept -> Level & {

    return with_side<RestingSide>([&](auto &above, auto &dense, [[maybe_unused]] auto &occupied,
                                      auto &below, auto & /*best_slot*/) -> Level & {
        if (price > upper_price_) {
            auto it = overflow_find_level<RestingSide>(above, price);
            assert(it != above.end() && "above oveflow level must exist");
            return it->second;
        } else if (price >= base_price_) {
            const std::size_t slot = price_diff_to_size_t(price, base_price_);
            assert(is_occupied(occupied, slot) && "dense level must exist");
            return dense[slot];
        } else {
            auto it = overflow_find_level<RestingSide>(below, price);
            assert(it != below.end() && "below overflow level must exist");
            return it->second;
        }
    });
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide>
void DenseLadderOrderBook<BandWidth, Hash>::remove_resting_at(
    Price price, std::uint32_t resting_index) noexcept {

    RestingOrderNode &node = resting_order_pool_[resting_index];

    auto from_overflow = [&](OverflowLevels &levels) {
        auto it = std::lower_bound(
            levels.begin(), levels.end(), price,
            [](const auto &entry, Price value) { return worse<RestingSide>(value, entry.first); });

        assert(it != levels.end() && it->first == price && "resting order's level must exist");

        remove_resting_order(it->second, node, resting_index);
        if (it->second.head == invalid_index)
            levels.erase(it);
    };

    with_side<RestingSide>(
        [&](auto &above, auto &dense, auto &occupied, auto &below, auto &best_slot) {
            if (price > upper_price_) {
                from_overflow(above);
            } else if (price >= base_price_) {
                const std::size_t slot = price_diff_to_size_t(price, base_price_);
                remove_resting_order(dense[slot], node, resting_index);
                if (dense[slot].head == invalid_index) {
                    clear_occupied(occupied, slot);
                    if (slot == best_slot) {
                        best_slot = next_worse_dense_slot<RestingSide>(occupied, slot);
                    }
                }
            } else {
                from_overflow(below);
            }
        });
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
AddResult DenseLadderOrderBook<BandWidth, Hash>::add_validated_order(const NewOrder &order,
                                                                     TradeWriter &trade_writer) {
    Quantity remaining = order.quantity;
    MatchOutcome match_outcome;
    if (order.side == Side::Buy)
        match_outcome = order.stp_id != StpId{0}
                            ? match_order<Side::Buy, true>(remaining, order, trade_writer)
                            : match_order<Side::Buy, false>(remaining, order, trade_writer);
    else
        match_outcome = order.stp_id != StpId{0}
                            ? match_order<Side::Sell, true>(remaining, order, trade_writer)
                            : match_order<Side::Sell, false>(remaining, order, trade_writer);

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

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_level(Level &level, Quantity &remaining,
                                                        Price level_price, const NewOrder &order,
                                                        TradeWriter &trade_writer) -> MatchOutcome {

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
                    remove_from_order_index(node.id);
                    remove_resting_order(level, node, node_index);

                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelNew:
                    return MatchOutcome::Aborted;
                case SelfTradeResolve::CancelResting:
                    node_index = node.next;
                    remove_from_order_index(node.id);
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
            remove_from_order_index(node.id);
            remove_resting_order(level, node,
                                 node_index_copy); // includes removing the quantity from the
                                                   // level.total_quantity
            if (emit_trade) {
                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = node.id,
                                  .price = level_price,
                                  .quantity = node.quantity,
                                  .aggressive_side = AggressiveSide};

                trade_writer.on_trade(trade);
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

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide, typename LevelsType>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::walk_dense(LevelsType &levels, auto &occupied,
                                                               auto &best_slot, Price base,
                                                               Price limit, auto &&visit) {
    constexpr bool mutating = !std::is_const_v<LevelsType>;

    std::size_t best_slot_local = best_slot;
    std::size_t slot = best_slot_local;

    while (slot != invalid_index) {

        Price price = base + Price{static_cast<std::int64_t>(slot)};
        if (worse<RestingSide>(price, limit))
            break;
        auto &level = levels[slot];
        const MatchOutcome result = visit(level, price);

        const std::size_t next = next_worse_dense_slot<RestingSide>(occupied, slot);

        if constexpr (mutating) {

            if (level.head == invalid_index) {
                clear_occupied(occupied, slot); // clear slot bit in occupied
                if (slot == best_slot_local)
                    best_slot_local = next;
            }
        }

        if (result != MatchOutcome::Exhausted) {
            if constexpr (mutating)
                best_slot = best_slot_local;
            return result;
        }

        slot = next;
    }

    if constexpr (mutating)
        best_slot = best_slot_local;
    return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::walk_side(this auto &self, Price limit,
                                                              auto &&visit) {
    auto walk = [&](auto &better_overflow, auto &dense, auto &occupied, auto &worse_overflow,
                    auto &best_slot) {
        MatchOutcome result = walk_overflow<RestingSide>(better_overflow, limit, visit);
        if (result == MatchOutcome::Exhausted)
            result =
                walk_dense<RestingSide>(dense, occupied, best_slot, self.base_price, limit, visit);
        if (result == MatchOutcome::Exhausted)
            result = walk_overflow<RestingSide>(worse_overflow, limit, visit);
        return result;
    };

    if constexpr (RestingSide == Side::Buy)
        return walk(self.bids_better_overflow_, self.bids_, self.bids_occupied_,
                    self.bids_worse_overflow_, self.best_bid_slot_);
    else
        return walk(self.asks_better_overflow_, self.asks_, self.asks_occupied_,
                    self.asks_worse_overflow_, self.best_ask_slot_);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::match_order(Quantity &remaining,
                                                                const NewOrder &order,
                                                                TradeWriter &trade_writer) {

    Price limit = effective_limit(order);

    constexpr Side RestingSide = AggressiveSide == Side::Buy ? Side::Sell : Side::Buy;

    auto visit = [&](Level &level, Price price) {
        return match_level<AggressiveSide, StpActive>(level, remaining, price, order, trade_writer);
    };

    return walk_side<RestingSide>(limit, visit);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::append_to_level(Level &level, std::uint32_t node_index,
                                                            Quantity quantity) noexcept {
    if (level.head == invalid_index) {
        level.head = node_index;
        level.tail = node_index;
        resting_order_pool_[node_index].prev = invalid_index;
        level.total_quantity = quantity;
    } else {
        resting_order_pool_[node_index].prev = level.tail;
        resting_order_pool_[level.tail].next = node_index;
        level.tail = node_index;
        level.total_quantity += quantity;
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide>
void DenseLadderOrderBook<BandWidth, Hash>::rest_overflow(OverflowLevels &levels, Price price,
                                                          std::uint32_t node_index,
                                                          Quantity quantity) {
    auto it =
        std::lower_bound(levels.begin(), levels.end(), price, [](const auto &entry, Price value) {
            return worse<RestingSide>(value, entry.first);
        });

    if (it == levels.end() || it->first != price) {
        it = levels.insert(it, {price, Level{}});
    }

    append_to_level(it->second, node_index, quantity);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide>
void DenseLadderOrderBook<BandWidth, Hash>::rest_dense(Levels &levels, Occupancy &occupied,
                                                       std::size_t &best_slot, Price price,
                                                       std::uint32_t node_index,
                                                       Quantity quantity) noexcept {

    assert(price >= base_price_ && price <= upper_price_ && "price must be inside the band");
    const std::size_t slot = price_diff_to_size_t(price, base_price_);

    append_to_level(levels[slot], node_index, quantity);

    set_occupied(occupied, slot);

    if (best_slot == invalid_index || worse<RestingSide>(best_slot, slot))
        best_slot = slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide>
AddResult DenseLadderOrderBook<BandWidth, Hash>::rest_order(Quantity remaining,
                                                            const NewOrder &order) {

    assert(order.order_type == OrderType::Limit && "only limit orders rest");

    if (resting_order_pool_head_ == invalid_index) {
        return AddResult{
            .remaining = remaining, .status = AddStatus::BookFull, .outcome = MatchOutcome::None};
    }

    const Price price = order.price;
    const std::uint32_t new_node_index = resting_order_pool_head_;

    const std::size_t slot = probe_slot(order.id);
    assert(order_index_[slot].node_index == invalid_index &&
           "has to be invalid_index otherwise duplicate found which should be rejected at "
           "validation");
    order_index_[slot] =
        IdEntry{.id = order.id, .price = price, .node_index = new_node_index, .side = RestingSide};

    resting_order_pool_head_ = resting_order_pool_[new_node_index].next;

    RestingOrderNode &node = resting_order_pool_[new_node_index];
    node.id = order.id;
    node.quantity = remaining;
    node.stp_id = order.stp_id;
    node.time_in_force = order.time_in_force;
    node.next = invalid_index;

    auto rest = [&](auto &above, auto &dense, auto &occupied, auto &below, auto &best_slot) {
        if (price > upper_price_) {
            rest_overflow<RestingSide>(above, price, new_node_index, remaining);
        } else if (price >= base_price_) {
            rest_dense<RestingSide>(dense, occupied, best_slot, price, new_node_index, remaining);
        } else {
            rest_overflow<RestingSide>(below, price, new_node_index, remaining);
        }
    };

    if constexpr (RestingSide == Side::Buy) {
        rest(bids_better_overflow_, bids_, bids_occupied_, bids_worse_overflow_, best_bid_slot_);
    } else {
        rest(asks_worse_overflow_, asks_, asks_occupied_, asks_better_overflow_, best_ask_slot_);
    }

    return AddResult{
        .remaining = remaining, .status = AddStatus::Rested, .outcome = MatchOutcome::Exhausted};
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
    const std::size_t slot = probe_slot(id);
    return order_index_[slot].node_index == invalid_index ? invalid_index : slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
std::size_t DenseLadderOrderBook<BandWidth, Hash>::lower_occupied_slot(const Occupancy &occupied,
                                                                       std::size_t slot) noexcept {
    if (slot == 0)
        return invalid_index;

    std::size_t word_index = slot >> 6; // /64 keep first bits, except the last 6
    const std::uint64_t bit_index = static_cast<std::uint64_t>(slot & 63); // the last 6 bits only

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
std::size_t DenseLadderOrderBook<BandWidth, Hash>::higher_occupied_slot(const Occupancy &occupied,
                                                                        std::size_t slot) noexcept {
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
template <Side RestingSide>
std::size_t
DenseLadderOrderBook<BandWidth, Hash>::next_worse_dense_slot(const Occupancy &occupied,
                                                             std::size_t slot) noexcept {
    if constexpr (RestingSide == Side::Buy)
        return lower_occupied_slot(occupied, slot);
    else
        return higher_occupied_slot(occupied, slot);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <bool ExcludeOrder, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::scan_level(const Level &level, Price level_price,
                                                       const NewOrder &order, Quantity &remaining,
                                                       const ExcludedOrder &excluded) const noexcept
    -> MatchOutcome {
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

        if constexpr (ExcludeOrder) {
            if (level_price == excluded.price)
                available -= excluded.quantity;
        }

        if (available >= remaining)
            return MatchOutcome::Filled;

        remaining -= available;
    }

    return MatchOutcome::Exhausted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide, bool StpActive>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(const NewOrder &order) const noexcept {
    return can_fill_levels<RestingSide, false, StpActive>(order, 0);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side RestingSide, bool ExcludeOrder, bool StpActive>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(
    const NewOrder &order, std::size_t excluded_slot) const noexcept {
    Quantity remaining = order.quantity;
    const Price limit = effective_limit(order);
    ExcludedOrder excluded{.node_index = invalid_index, .price = Price{}, .quantity = Quantity{}};

    if constexpr (ExcludeOrder) {
        assert(excluded_slot < order_index_.size() &&
               "excluded_slot must be a valid index into order_index_");
        const IdEntry &entry = order_index_[excluded_slot];

        if (entry.side == RestingSide) {
            excluded.node_index = entry.node_index;
            excluded.price = entry.price;
            excluded.quantity = resting_order_pool_[entry.node_index].quantity;
        }
        // if we have the excluded order on the wrong side, just mark it with node index
        // invalid_index and quantity 0
    }

    auto visit = [&](const Level &level, Price price) {
        return scan_level<ExcludeOrder, StpActive>(level, price, order, remaining, excluded);
    };

    return walk_side<RestingSide>(limit, visit) == MatchOutcome::Filled;
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