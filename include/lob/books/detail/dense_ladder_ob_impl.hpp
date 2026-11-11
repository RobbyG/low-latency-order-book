// Included by <lob/books/dense_ladder_ob.hpp>. Do not include directly.

#pragma once

#include <lob/books/dense_ladder_ob.hpp>
#include <lob/event_writer.hpp>
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
                                                           EventWriter &trade_writer) {
    assert(order.quantity != Quantity{0} && "a new order cannot have a quantity of 0");

    AddStatus status = validate_new_order(order, true);
    if (status != AddStatus::Accepted) {
        return AddResult{
            .remaining = order.quantity, .status = status, .outcome = MatchOutcome::None};
    }

    return add_validated_order(order, trade_writer);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CancelResult DenseLadderOrderBook<BandWidth, Hash>::cancel_order(OrderId id) noexcept {
    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_resting_index)
        return CancelResult{.quantity = Quantity{0}, .status = CancelStatus::NotFound};

    const Quantity cancelled_quantity =
        resting_order_pool_[order_location_table_[slot].resting_index].quantity;

    cancel_order_at_slot(slot);

    return CancelResult{.quantity = cancelled_quantity, .status = CancelStatus::Cancelled};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
ReduceResult DenseLadderOrderBook<BandWidth, Hash>::reduce_order_by(OrderId id,
                                                                    Quantity quantity) noexcept {

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
ReplaceResult DenseLadderOrderBook<BandWidth, Hash>::replace_order(OrderId id,
                                                                   const NewOrder &order,
                                                                   EventWriter &trade_writer) {
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

    AddResult result = add_validated_order(order, trade_writer);

    return ReplaceResult{.new_quantity = result.remaining,
                         .add_status = result.status,
                         .cancel_status = CancelStatus::Cancelled,
                         .outcome = result.outcome};
}

// L1 view API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::best_bid(PriceQuantity &pq) const noexcept {
    return best_level<Side::Buy>(pq);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::best_ask(PriceQuantity &pq) const noexcept {
    return best_level<Side::Sell>(pq);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::best_price_quantity(Side side,
                                                                PriceQuantity &pq) const noexcept {
    return side == Side::Buy ? best_bid(pq) : best_ask(pq);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
bool DenseLadderOrderBook<BandWidth, Hash>::best_level(PriceQuantity &pq) const noexcept {

    return with_side<S>([&](auto &better, auto &dense, auto & /*occupied*/, auto &worse,
                            auto &best_slot) -> bool {
        if (!better.empty()) {
            pq = PriceQuantity{.price = better.front().first,
                               .quantity = better.front().second.total_quantity};
            return true;
        }
        if (best_slot != invalid_resting_index) {
            pq = PriceQuantity{.price = ladder_low_ + Price{static_cast<std::int64_t>(best_slot)},
                               .quantity = dense[best_slot].total_quantity};
            return true;
        }
        if (!worse.empty()) {
            pq = PriceQuantity{.price = worse.front().first,
                               .quantity = worse.front().second.total_quantity};
            return true;
        }

        pq = PriceQuantity{.price = Price{0}, .quantity = Quantity{0}};
        return false;
    });
}

// L2 view API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult
DenseLadderOrderBook<BandWidth, Hash>::copy_bid_depth(std::span<PriceLevel> out) const noexcept {
    return copy_levels<Side::Buy>(out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult
DenseLadderOrderBook<BandWidth, Hash>::copy_ask_depth(std::span<PriceLevel> out) const noexcept {
    return copy_levels<Side::Sell>(out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult
DenseLadderOrderBook<BandWidth, Hash>::copy_depth(Side side,
                                                  std::span<PriceLevel> out) const noexcept {
    return side == Side::Buy ? copy_bid_depth(out) : copy_ask_depth(out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
CopyResult
DenseLadderOrderBook<BandWidth, Hash>::copy_levels(std::span<PriceLevel> out) const noexcept {
    constexpr Price limit = S == Side::Buy ? min_price : max_price;
    std::uint32_t written = 0;

    if (!out.empty()) {
        (void)walk_side<S>(limit, [&](const Level &level, Price price) {
            out[written++] =
                PriceLevel{.price = price, .total_quantity = level.total_quantity, .side = S};
            return written == out.size() ? MatchOutcome::Filled : MatchOutcome::Exhausted;
        });
    }

    return CopyResult{.written = written, .available = level_count<S>()};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
std::uint32_t DenseLadderOrderBook<BandWidth, Hash>::level_count() const noexcept {
    return with_side<S>([](const auto &better, const auto &, const auto &occupied,
                           const auto &worse, const auto &) {
        std::uint32_t n = static_cast<std::uint32_t>(better.size() + worse.size());
        for (std::uint64_t word : occupied) {
            n += static_cast<std::uint32_t>(std::popcount(word));
        }
        return n;
    });
}

// L3 view API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::find_order(OrderId id, OrderView &out) const noexcept {
    const std::size_t slot = find_id_entry(id);

    if (slot != invalid_resting_index) {
        const OrderLocation &entry = order_location_table_[slot];
        const RestingOrderNode &resting_order = resting_order_pool_[entry.resting_index];

        out = OrderView{.id = entry.id,
                        .price = entry.price,
                        .quantity = resting_order.quantity,
                        .stp_id = resting_order.stp_id,
                        .side = entry.side,
                        .time_in_force = resting_order.time_in_force};

        return true;
    }

    return false;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult DenseLadderOrderBook<BandWidth, Hash>::copy_best_bid_orders(
    std::span<OrderView> out) const noexcept {
    Price price;
    const Level &level = best_price_level<Side::Buy>(price);

    return copy_level<Side::Buy>(level, price, out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult DenseLadderOrderBook<BandWidth, Hash>::copy_best_ask_orders(
    std::span<OrderView> out) const noexcept {
    Price price;
    const Level &level = best_price_level<Side::Sell>(price);

    return copy_level<Side::Sell>(level, price, out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult DenseLadderOrderBook<BandWidth, Hash>::copy_orders_at_price(
    Side side, Price price, std::span<OrderView> out) const noexcept {

    Level &level = get_level<side>(price);

    return copy_level<side>(level, price, out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
auto DenseLadderOrderBook<BandWidth, Hash>::best_price_level(Price &price) const noexcept
    -> const Level * {
    return with_side<S>([&](auto &better, auto &dense, auto & /*occupied*/, auto &worse,
                            auto &best_slot) -> const Level * {
        if (!better.empty()) {
            price = better.front().first;
            return &better.front().second;
        }
        if (best_slot != invalid_resting_index) {
            price = ladder_low_ + Price{static_cast<std::int64_t>(best_slot)};
            return &dense[best_slot];
        }
        if (!worse.empty()) {
            price = worse.front().first;
            return &worse.front().second;
        }

        return nullptr;
    });
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
CopyResult
DenseLadderOrderBook<BandWidth, Hash>::copy_level(const Level &level, Price price,
                                                  std::span<OrderView> out) const noexcept {
    std::uint32_t written = 0;
    std::uint32_t available = 0;

    std::size_t resting_index = level.head;

    while (resting_index != invalid_resting_index) {
        const RestingOrderNode &resting_order = resting_order_pool_[resting_index];
        if (written < out.size()) {
            out[written++] = Orderview{.id = resting_order.id,
                                       .price = price,
                                       .quantity = resting_order.quantity,
                                       .stp_id = resting_order.stp_id,
                                       .side = S,
                                       .time_in_force = resting_order.time_in_force};
        }

        available++;
        resting_index = resting_order.next;
    }

    return CopyResult{.written = written, .available = available};
}

// private functions

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::reserve(const Config &config) {
    ladder_low_ = config.base;
    ladder_high_ = config.base + Price{BandWidth - 1};

    resting_order_pool_.resize(config.max_orders);

    if (config.max_orders == 0)
        resting_order_pool_head_ = invalid_resting_index;
    else {
        resting_order_pool_head_ = 0;

        for (std::uint32_t i = 0; i < config.max_orders - 1; ++i)
            resting_order_pool_[i].next = i + 1;

        resting_order_pool_[config.max_orders - 1].next = invalid_resting_index;
    }

    std::size_t hash_slots =
        std::max<std::size_t>(2, static_cast<std::size_t>(config.max_orders) * 2);
    hash_slots = std::bit_ceil(hash_slots);

    hash_shift_ = static_cast<unsigned>(64 - std::countr_zero(hash_slots));
    order_location_table_.resize(hash_slots);
    hash_mask_ = hash_slots - 1;

    assert(std::has_single_bit(order_location_table_.size()) &&
           "The hash map (vector) must have a power of 2 size.");
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::erase_slot_from_order_index(std::size_t slot) noexcept {
    const std::size_t mask = hash_mask_;
    order_location_table_[slot].resting_index = invalid_resting_index;
    std::size_t empty_slot = slot;

    slot = (slot + 1) & mask;
    while (order_location_table_[slot].resting_index != invalid_resting_index) {
        OrderLocation &entry_to_check = order_location_table_[slot];
        const std::size_t home = Hash::hash_into_slot(entry_to_check.id, mask, hash_shift_);

        if (((slot - home) & mask) >= ((slot - empty_slot) & mask)) {
            order_location_table_[empty_slot] = entry_to_check;
            empty_slot = slot;
        }
        slot = (slot + 1) & mask;
    }
    order_location_table_[empty_slot].resting_index = invalid_resting_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::cancel_order_at_slot(std::size_t slot) noexcept {
    const OrderLocation entry = order_location_table_[slot];

    if (entry.side == Side::Buy)
        remove_resting_at<Side::Buy>(entry.price, entry.resting_index);
    else
        remove_resting_at<Side::Sell>(entry.price, entry.resting_index);

    erase_slot_from_order_index(slot);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::remove_from_order_index(OrderId id) noexcept {
    const std::size_t slot = find_id_entry(id);
    if (slot == invalid_resting_index)
        return false;

    erase_slot_from_order_index(slot);

    return true;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
auto DenseLadderOrderBook<BandWidth, Hash>::get_level(Price price) noexcept -> Level & {
    return with_side<S>([&](auto &above, auto &dense, [[maybe_unused]] auto &occupied, auto &below,
                            auto & /*best_slot*/) -> Level & {
        if (price > ladder_high_) {
            auto it = overflow_find_level<S>(above, price);
            assert(it != above.end() && "above oveflow level must exist");
            return it->second;
        } else if (price >= ladder_low_) {
            const std::size_t slot = price_diff_to_size_t(price, ladder_low_);
            assert(is_occupied(occupied, slot) && "dense level must exist");
            return dense[slot];
        } else {
            auto it = overflow_find_level<S>(below, price);
            assert(it != below.end() && "below overflow level must exist");
            return it->second;
        }
    });
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
void DenseLadderOrderBook<BandWidth, Hash>::remove_resting_at(
    Price price, std::uint32_t resting_index) noexcept {
    RestingOrderNode &node = resting_order_pool_[resting_index];

    auto from_overflow = [&](OverflowLevels &levels) {
        auto it = std::lower_bound(
            levels.begin(), levels.end(), price,
            [](const auto &entry, Price value) { return worse<S>(value, entry.first); });

        assert(it != levels.end() && it->first == price && "resting order's level must exist");

        remove_resting_order(it->second, node, resting_index);
        if (it->second.head == invalid_resting_index)
            levels.erase(it);
    };

    with_side<S>([&](auto &above, auto &dense, auto &occupied, auto &below, auto &best_slot) {
        if (price > ladder_high_) {
            from_overflow(above);
        } else if (price >= ladder_low_) {
            const std::size_t slot = price_diff_to_size_t(price, ladder_low_);
            remove_resting_order(dense[slot], node, resting_index);
            if (dense[slot].head == invalid_resting_index) {
                clear_occupied(occupied, slot);
                if (slot == best_slot) {
                    best_slot = next_worse_dense_slot<S>(occupied, slot);
                }
            }
        } else {
            from_overflow(below);
        }
    });
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::remove_resting_order(
    Level &level, RestingOrderNode &node, std::uint32_t resting_index) noexcept {
    level.total_quantity -= node.quantity;
    if (node.prev == invalid_resting_index) {
        level.head = node.next;
        if (node.next != invalid_resting_index)
            resting_order_pool_[node.next].prev = node.prev;
        else
            level.tail = invalid_resting_index;
    } else if (node.next == invalid_resting_index) {
        resting_order_pool_[node.prev].next = invalid_resting_index;
        level.tail = node.prev;
    } else {
        resting_order_pool_[node.prev].next = node.next;
        resting_order_pool_[node.next].prev = node.prev;
    }

    node.next = resting_order_pool_head_;
    resting_order_pool_head_ = resting_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
AddStatus
DenseLadderOrderBook<BandWidth, Hash>::validate_new_order(const NewOrder &order,
                                                          bool check_duplicate) const noexcept {
    if (check_duplicate) {
        std::size_t slot = find_id_entry(order.id);
        if (slot != invalid_resting_index) {
            return AddStatus::DuplicateOrderId;
        }
    }

    if (order.time_in_force == TimeInForce::Fok && !can_fully_fill(order))
        return AddStatus::WouldNotFullyFill;

    return AddStatus::Accepted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
AddResult DenseLadderOrderBook<BandWidth, Hash>::add_validated_order(const NewOrder &order,
                                                                     EventWriter &trade_writer) {
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

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
auto DenseLadderOrderBook<BandWidth, Hash>::match_level(Level &level, Quantity &remaining,
                                                        Price level_price, const NewOrder &order,
                                                        EventWriter &trade_writer) -> MatchOutcome {
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

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S, typename LevelsType>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::walk_overflow(LevelsType &levels, Price limit,
                                                                  auto &&visit) {
    constexpr bool mutating = !std::is_const_v<LevelsType>;

    for (auto it = levels.begin(); it != levels.end();) {

        auto &[price, level] = *it;
        if (worse<S>(price, limit))
            break;

        const MatchOutcome outcome = visit(level, price);

        if constexpr (mutating) {
            if (level.head == invalid_resting_index)
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

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S, typename LevelsType>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::walk_dense(LevelsType &levels, auto &occupied,
                                                               auto &best_slot, Price base,
                                                               Price limit, auto &&visit) {
    constexpr bool mutating = !std::is_const_v<LevelsType>;

    std::size_t best_slot_local = best_slot;
    std::size_t slot = best_slot_local;

    while (slot != invalid_resting_index) {

        Price price = base + Price{static_cast<std::int64_t>(slot)};
        if (worse<S>(price, limit))
            break;
        auto &level = levels[slot];
        const MatchOutcome result = visit(level, price);

        const std::size_t next = next_worse_dense_slot<S>(occupied, slot);

        if constexpr (mutating) {

            if (level.head == invalid_resting_index) {
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

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::walk_side(this auto &self, Price limit,
                                                              auto &&visit) {
    auto walk = [&](auto &better_overflow, auto &dense, auto &occupied, auto &worse_overflow,
                    auto &best_slot) {
        MatchOutcome result = self.template walk_overflow<S>(better_overflow, limit, visit);
        if (result == MatchOutcome::Exhausted)
            result = self.template walk_dense<S>(dense, occupied, best_slot, self.ladder_low_,
                                                 limit, visit);
        if (result == MatchOutcome::Exhausted)
            result = self.template walk_overflow<S>(worse_overflow, limit, visit);
        return result;
    };

    if constexpr (S == Side::Buy)
        return walk(self.bids_better_overflow_, self.bids_, self.bids_occupied_,
                    self.bids_worse_overflow_, self.bids_best_slot_);
    else
        return walk(self.asks_better_overflow_, self.asks_, self.asks_occupied_,
                    self.asks_worse_overflow_, self.asks_best_slot_);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side AggressiveSide, bool StpActive>
MatchOutcome DenseLadderOrderBook<BandWidth, Hash>::match_order(Quantity &remaining,
                                                                const NewOrder &order,
                                                                EventWriter &trade_writer) {
    Price limit = effective_limit(order);

    constexpr Side RestingSide = AggressiveSide == Side::Buy ? Side::Sell : Side::Buy;

    auto visit = [&](Level &level, Price price) {
        return match_level<AggressiveSide, StpActive>(level, remaining, price, order, trade_writer);
    };

    return walk_side<RestingSide>(limit, visit);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::append_to_level(Level &level,
                                                            std::uint32_t resting_index,
                                                            Quantity quantity) noexcept {
    if (level.head == invalid_resting_index) {
        level.head = resting_index;
        level.tail = resting_index;
        resting_order_pool_[resting_index].prev = invalid_resting_index;
        level.total_quantity = quantity;
    } else {
        resting_order_pool_[resting_index].prev = level.tail;
        resting_order_pool_[level.tail].next = resting_index;
        level.tail = resting_index;
        level.total_quantity += quantity;
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
void DenseLadderOrderBook<BandWidth, Hash>::rest_overflow(OverflowLevels &levels, Price price,
                                                          std::uint32_t resting_index,
                                                          Quantity quantity) {
    auto it =
        std::lower_bound(levels.begin(), levels.end(), price, [](const auto &entry, Price value) {
            return worse<S>(value, entry.first);
        });

    if (it == levels.end() || it->first != price) {
        it = levels.insert(it, {price, Level{}});
    }

    append_to_level(it->second, resting_index, quantity);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
void DenseLadderOrderBook<BandWidth, Hash>::rest_dense(Ladder &levels, LadderOccupancy &occupied,
                                                       std::size_t &best_slot, Price price,
                                                       std::uint32_t resting_index,
                                                       Quantity quantity) noexcept {
    assert(price >= ladder_low_ && price <= ladder_high_ && "price must be inside the band");
    const std::size_t slot = price_diff_to_size_t(price, ladder_low_);

    append_to_level(levels[slot], resting_index, quantity);

    set_occupied(occupied, slot);

    if (best_slot == invalid_resting_index || worse<S>(best_slot, slot))
        best_slot = slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
AddResult DenseLadderOrderBook<BandWidth, Hash>::rest_order(Quantity remaining,
                                                            const NewOrder &order) {
    assert(order.order_type == OrderType::Limit && "only limit orders rest");

    if (resting_order_pool_head_ == invalid_resting_index) {
        return AddResult{
            .remaining = remaining, .status = AddStatus::BookFull, .outcome = MatchOutcome::None};
    }

    const Price price = order.price;
    const std::uint32_t new_node_index = resting_order_pool_head_;

    const std::size_t slot = probe_slot(order.id);
    assert(order_location_table_[slot].resting_index == invalid_resting_index &&
           "has to be invalid_resting_index otherwise duplicate found which should be rejected at "
           "validation");
    order_location_table_[slot] =
        OrderLocation{.id = order.id, .price = price, .resting_index = new_node_index, .side = S};

    resting_order_pool_head_ = resting_order_pool_[new_node_index].next;

    RestingOrderNode &node = resting_order_pool_[new_node_index];
    node.id = order.id;
    node.quantity = remaining;
    node.stp_id = order.stp_id;
    node.time_in_force = order.time_in_force;
    node.next = invalid_resting_index;

    auto rest = [&](auto &above, auto &dense, auto &occupied, auto &below, auto &best_slot) {
        if (price > ladder_high_) {
            rest_overflow<S>(above, price, new_node_index, remaining);
        } else if (price >= ladder_low_) {
            rest_dense<S>(dense, occupied, best_slot, price, new_node_index, remaining);
        } else {
            rest_overflow<S>(below, price, new_node_index, remaining);
        }
    };

    if constexpr (S == Side::Buy) {
        rest(bids_better_overflow_, bids_, bids_occupied_, bids_worse_overflow_, bids_best_slot_);
    } else {
        rest(asks_worse_overflow_, asks_, asks_occupied_, asks_better_overflow_, asks_best_slot_);
    }

    return AddResult{
        .remaining = remaining, .status = AddStatus::Rested, .outcome = MatchOutcome::Exhausted};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t DenseLadderOrderBook<BandWidth, Hash>::probe_slot(OrderId id) const noexcept {
    std::size_t slot = Hash::hash_into_slot(id, hash_mask_, hash_shift_);
    [[maybe_unused]] std::size_t probes = 0;

    while (true) {
        const OrderLocation &entry = order_location_table_[slot];

        if (entry.resting_index == invalid_resting_index || entry.id == id)
            return slot;

        slot = (slot + 1) & hash_mask_;
        assert(++probes < order_location_table_.size() && "table full");
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t DenseLadderOrderBook<BandWidth, Hash>::find_id_entry(OrderId id) const noexcept {
    const std::size_t slot = probe_slot(id);
    return order_location_table_[slot].resting_index == invalid_resting_index
               ? invalid_resting_index
               : slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t
DenseLadderOrderBook<BandWidth, Hash>::lower_occupied_slot(const LadderOccupancy &occupied,
                                                           std::size_t slot) noexcept {
    if (slot == 0)
        return invalid_resting_index;

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

    return invalid_resting_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t
DenseLadderOrderBook<BandWidth, Hash>::higher_occupied_slot(const LadderOccupancy &occupied,
                                                            std::size_t slot) noexcept {
    if (slot == BandWidth - 1)
        return invalid_resting_index;

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

    return invalid_resting_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
std::size_t
DenseLadderOrderBook<BandWidth, Hash>::next_worse_dense_slot(const LadderOccupancy &occupied,
                                                             std::size_t slot) noexcept {
    if constexpr (S == Side::Buy)
        return lower_occupied_slot(occupied, slot);
    else
        return higher_occupied_slot(occupied, slot);
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