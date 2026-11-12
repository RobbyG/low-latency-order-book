// included by <lob/books/dense_ladder_core.hpp>. Do not include directly.

#pragma once

#include <lob/books/dense_ladder_core.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace lob::books {

// construction

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
DenseLadderCore<BandWidth, Hash>::DenseLadderCore(Config config) : config_(config) {
    reserve(config_);
    reset();
}

// views
// L1 view API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderCore<BandWidth, Hash>::best_bid(PriceQuantity &pq) const noexcept {
    return best_price_level<Side::Buy>(pq);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderCore<BandWidth, Hash>::best_ask(PriceQuantity &pq) const noexcept {
    return best_price_level<Side::Sell>(pq);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderCore<BandWidth, Hash>::best_price_quantity(Side side,
                                                           PriceQuantity &pq) const noexcept {
    return side == Side::Buy ? best_bid(pq) : best_ask(pq);
}

// L2 view API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult
DenseLadderCore<BandWidth, Hash>::copy_bid_depth(std::span<PriceLevel> out) const noexcept {
    return copy_levels<Side::Buy>(out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult
DenseLadderCore<BandWidth, Hash>::copy_ask_depth(std::span<PriceLevel> out) const noexcept {
    return copy_levels<Side::Sell>(out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult DenseLadderCore<BandWidth, Hash>::copy_depth(Side side,
                                                        std::span<PriceLevel> out) const noexcept {
    return side == Side::Buy ? copy_bid_depth(out) : copy_ask_depth(out);
}

// L3 view API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
bool DenseLadderCore<BandWidth, Hash>::find_order(OrderId id, OrderView &out) const noexcept {
    const std::size_t slot = find_order_location(id);

    if (slot != invalid_hash_slot) {
        const OrderLocation &entry = order_location_table_[slot];
        const RestingOrderNode &resting_order = resting_order_pool_[entry.resting_index];

        out = OrderView{.id = id,
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
CopyResult
DenseLadderCore<BandWidth, Hash>::copy_best_bid_orders(std::span<OrderView> out) const noexcept {
    Price price{};
    const Level *level = best_price_level<Side::Buy>(price);
    if (level == nullptr)
        return CopyResult{.written = 0, .available = 0};

    return copy_level<Side::Buy>(*level, price, out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult
DenseLadderCore<BandWidth, Hash>::copy_best_ask_orders(std::span<OrderView> out) const noexcept {
    Price price{};
    const Level *level = best_price_level<Side::Sell>(price);

    if (level == nullptr)
        return CopyResult{.written = 0, .available = 0};

    return copy_level<Side::Sell>(*level, price, out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CopyResult
DenseLadderCore<BandWidth, Hash>::copy_orders_at_price(Side side, Price price,
                                                       std::span<OrderView> out) const noexcept {

    return side == Side::Buy ? copy_orders_at_price<Side::Buy>(price, out)
                             : copy_orders_at_price<Side::Sell>(price, out);
}

// extra public API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderCore<BandWidth, Hash>::reset() noexcept {

    std::uint32_t local_max_orders = config_.max_orders;

    bids_.fill(Level{});
    asks_.fill(Level{});
    bids_occupied_.fill(0);
    asks_occupied_.fill(0);
    bids_best_slot_ = invalid_price_slot;
    asks_best_slot_ = invalid_price_slot;

    bids_better_overflow_.clear();
    bids_worse_overflow_.clear();
    asks_better_overflow_.clear();
    asks_worse_overflow_.clear();

    std::fill(order_location_table_.begin(), order_location_table_.end(), OrderLocation{});

    resting_order_pool_head_ = local_max_orders == 0 ? invalid_resting_index : 0;
    for (std::uint32_t i = 0; i + 1 < local_max_orders; ++i)
        resting_order_pool_[i].next = i + 1;
    if (local_max_orders != 0)
        resting_order_pool_[local_max_orders - 1].next = invalid_resting_index;
}

// protected: derived class API

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
void DenseLadderCore<BandWidth, Hash>::walk_side(this auto &self, Price limit, auto &&visit) {
    self.template with_side<S>([&](auto &better_overflow, auto &ladder, auto &occupied,
                                   auto &best_slot, auto &worse_overflow) {
        WalkControl outcome = self.template walk_overflow<S>(better_overflow, limit, visit);
        if (outcome == WalkControl::Continue)
            outcome = self.template walk_ladder<S>(ladder, occupied, best_slot, self.ladder_low_,
                                                   limit, visit);
        if (outcome == WalkControl::Continue)
            outcome = self.template walk_overflow<S>(worse_overflow, limit, visit);
    });
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
AddResult DenseLadderCore<BandWidth, Hash>::rest_order(Quantity remaining, const NewOrder &order) {
    assert(order.order_type == OrderType::Limit && "only limit orders rest");

    if (resting_order_pool_head_ == invalid_resting_index) {
        return AddResult{
            .remaining = remaining, .status = AddStatus::BookFull, .outcome = MatchOutcome::None};
    }

    const Price price = order.price;
    const std::uint32_t new_node_index = resting_order_pool_head_;

    const std::size_t slot = probe_slot(order.id);
    assert(order_location_table_[slot].resting_index == invalid_resting_index &&
           "has to be invalid_resting_index otherwise duplicate found which should be rejected by "
           "earlier by the caller");
    order_location_table_[slot] =
        OrderLocation{.id = order.id, .price = price, .resting_index = new_node_index, .side = S};

    resting_order_pool_head_ = resting_order_pool_[new_node_index].next;

    RestingOrderNode &node = resting_order_pool_[new_node_index];
    node.id = order.id;
    node.quantity = remaining;
    node.stp_id = order.stp_id;
    node.time_in_force = order.time_in_force;
    node.next = invalid_resting_index;

    with_side<S>([&](auto &better_overflow, auto &ladder, auto &occupied, auto &best_slot,
                     auto &worse_overflow) {
        if (in_band(price))
            rest_ladder<S>(ladder, occupied, best_slot, price, new_node_index, remaining);
        else {
            auto &levels = better_than_band<S>(price) ? better_overflow : worse_overflow;
            rest_overflow<S>(levels, price, new_node_index, remaining);
        }
    });

    return AddResult{
        .remaining = remaining, .status = AddStatus::Rested, .outcome = MatchOutcome::Exhausted};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
CancelResult DenseLadderCore<BandWidth, Hash>::remove_order(OrderId id) noexcept {
    const std::size_t slot = find_order_location(id);
    if (slot == invalid_hash_slot)
        return CancelResult{.quantity = Quantity{0}, .status = CancelStatus::NotFound};

    const Quantity cancelled_quantity =
        resting_order_pool_[order_location_table_[slot].resting_index].quantity;

    remove_order_from_location(slot);

    return CancelResult{.quantity = cancelled_quantity, .status = CancelStatus::Cancelled};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
ReduceResult DenseLadderCore<BandWidth, Hash>::reduce_order(OrderId id,
                                                            Quantity quantity) noexcept {

    const Quantity reduce_quantity = quantity;

    assert(reduce_quantity > Quantity{0} && "the quantity to reduce by must be greater than 0");
    const std::size_t slot = find_order_location(id);
    if (slot == invalid_hash_slot)
        return ReduceResult{.new_quantity = Quantity{0},
                            .old_quantity = Quantity{0},
                            .status = ReduceStatus::NotFound};

    const OrderLocation entry = order_location_table_[slot];
    auto &order = resting_order_pool_[entry.resting_index];
    const Quantity old_quantity = order.quantity;

    if (old_quantity < reduce_quantity)
        return ReduceResult{.new_quantity = old_quantity,
                            .old_quantity = old_quantity,
                            .status = ReduceStatus::ExceedsRestingQuantity};

    if (old_quantity == reduce_quantity) {
        remove_order_from_location(slot);
        return ReduceResult{.new_quantity = Quantity{0},
                            .old_quantity = old_quantity,
                            .status = ReduceStatus::Cancelled};
    }

    Level *level = entry.side == Side::Buy ? find_level<Side::Buy>(entry.price)
                                           : find_level<Side::Sell>(entry.price);
    assert(level != nullptr && "level must exist for a resting order");
    level->total_quantity -= reduce_quantity;

    const Quantity new_quantity = old_quantity - reduce_quantity;
    order.quantity = new_quantity;
    return ReduceResult{.new_quantity = new_quantity,
                        .old_quantity = old_quantity,
                        .status = ReduceStatus::Reduced};
}

// private: internal helpers
// construction

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderCore<BandWidth, Hash>::reserve(const Config &config) {
    ladder_low_ = config.base;
    ladder_high_ = config.base + Price{BandWidth - 1};

    resting_order_pool_.resize(config.max_orders);

    std::size_t hash_slots =
        std::max<std::size_t>(2, static_cast<std::size_t>(config.max_orders) * 2);
    hash_slots = std::bit_ceil(hash_slots);

    hash_shift_ = static_cast<unsigned>(64 - std::countr_zero(hash_slots));
    order_location_table_.resize(hash_slots);
    hash_mask_ = hash_slots - 1;

    assert(std::has_single_bit(order_location_table_.size()) &&
           "The hash map (vector) must have a power of 2 size.");
}

// L1 view

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
bool DenseLadderCore<BandWidth, Hash>::best_price_level(PriceQuantity &pq) const noexcept {

    return with_side<S>([&](auto &better_overflow, auto &ladder, auto & /*occupied*/,
                            auto &best_slot, auto &worse_overflow) -> bool {
        if (!better_overflow.empty()) {
            pq = PriceQuantity{.price = better_overflow.front().first,
                               .quantity = better_overflow.front().second.total_quantity};
            return true;
        }
        if (best_slot != invalid_price_slot) {
            pq = PriceQuantity{.price = ladder_low_ + Price{static_cast<std::int64_t>(best_slot)},
                               .quantity = ladder[best_slot].total_quantity};
            return true;
        }
        if (!worse_overflow.empty()) {
            pq = PriceQuantity{.price = worse_overflow.front().first,
                               .quantity = worse_overflow.front().second.total_quantity};
            return true;
        }

        pq = PriceQuantity{.price = Price{0}, .quantity = Quantity{0}};
        return false;
    });
}

// L2 view

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
CopyResult DenseLadderCore<BandWidth, Hash>::copy_levels(std::span<PriceLevel> out) const noexcept {
    constexpr Price limit = S == Side::Buy ? min_price : max_price;
    std::uint32_t written = 0;

    if (!out.empty()) {
        (void)walk_side<S>(limit, [&](const Level &level, Price price) {
            out[written++] =
                PriceLevel{.price = price, .total_quantity = level.total_quantity, .side = S};
            return written == out.size() ? WalkControl::Stop : WalkControl::Continue;
        });
    }

    return CopyResult{.written = written, .available = level_count<S>()};
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
std::uint32_t DenseLadderCore<BandWidth, Hash>::level_count() const noexcept {
    return with_side<S>([](const auto &better, const auto &, const auto &occupied, const auto &,
                           const auto &worse) {
        std::uint32_t n = static_cast<std::uint32_t>(better.size() + worse.size());
        for (std::uint64_t word : occupied) {
            n += static_cast<std::uint32_t>(std::popcount(word));
        }
        return n;
    });
}

// L3 view

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
CopyResult
DenseLadderCore<BandWidth, Hash>::copy_orders_at_price(Price price,
                                                       std::span<OrderView> out) const noexcept {
    const Level *level = find_level<S>(price);
    if (level == nullptr)
        return CopyResult{.written = 0, .available = 0};

    return copy_level<S>(*level, price, out);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
auto DenseLadderCore<BandWidth, Hash>::best_price_level(Price &price) const noexcept
    -> const Level * {
    return with_side<S>([&](auto &better, auto &ladder, auto & /*occupied*/, auto &best_slot,
                            auto &worse) -> const Level * {
        if (!better.empty()) {
            price = better.front().first;
            return &better.front().second;
        }
        if (best_slot != invalid_price_slot) {
            price = ladder_low_ + Price{static_cast<std::int64_t>(best_slot)};
            return &ladder[best_slot];
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
CopyResult DenseLadderCore<BandWidth, Hash>::copy_level(const Level &level, Price price,
                                                        std::span<OrderView> out) const noexcept {
    std::uint32_t written = 0;
    std::uint32_t available = 0;

    std::size_t resting_index = level.head;

    while (resting_index != invalid_resting_index) {
        const RestingOrderNode &resting_order = resting_order_pool_[resting_index];
        if (written < out.size()) {
            out[written++] = OrderView{.id = resting_order.id,
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

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
auto DenseLadderCore<BandWidth, Hash>::find_level(Price price) const noexcept -> const Level * {
    return with_side<S>([&](auto &better_overflow, auto &ladder, auto &occupied,
                            auto & /*best_slot*/, auto &worse_overflow) -> const Level * {
        if (in_band(price)) {
            const std::size_t slot = price_diff_to_size_t(price, ladder_low_);
            return is_occupied(occupied, slot) ? &ladder[slot] : nullptr;
        }

        auto &levels = better_than_band<S>(price) ? better_overflow : worse_overflow;
        auto it = overflow_find_level<S>(levels, price);
        return (it != levels.end() && it->first == price) ? &it->second : nullptr;
    });
}

// walk

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S, typename LevelsType>
auto DenseLadderCore<BandWidth, Hash>::walk_overflow(LevelsType &levels, Price limit, auto &&visit)
    -> WalkControl {
    constexpr bool mutating = !std::is_const_v<LevelsType>;

    for (auto it = levels.begin(); it != levels.end();) {

        auto &[price, level] = *it;
        if (worse<S>(price, limit))
            break;

        const WalkControl outcome = visit(level, price);

        if constexpr (mutating) {
            if (level.head == invalid_resting_index)
                it = levels.erase(it);
            else
                ++it;
        } else {
            ++it;
        }

        if (outcome == WalkControl::Stop)
            return outcome;
    }

    return WalkControl::Continue;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S, typename LevelsType>
auto DenseLadderCore<BandWidth, Hash>::walk_ladder(LevelsType &levels, auto &occupied,
                                                   auto &best_slot, Price base, Price limit,
                                                   auto &&visit) -> WalkControl {
    constexpr bool mutating = !std::is_const_v<LevelsType>;

    std::size_t best_slot_local = best_slot;
    std::size_t slot = best_slot_local;

    while (slot != invalid_price_slot) {

        Price price = base + Price{static_cast<std::int64_t>(slot)};
        if (worse<S>(price, limit))
            break;

        auto &level = levels[slot];

        const WalkControl outcome = visit(level, price);

        const std::size_t next = next_worse_ladder_slot<S>(occupied, slot);

        if constexpr (mutating) {
            if (level.head == invalid_resting_index) {
                clear_occupied(occupied, slot);
                if (slot == best_slot_local)
                    best_slot_local = next;
            }
        }

        if (outcome == WalkControl::Stop) {
            if constexpr (mutating)
                best_slot = best_slot_local;
            return outcome;
        }

        slot = next;
    }

    if constexpr (mutating)
        best_slot = best_slot_local;
    return WalkControl::Continue;
}

// order management

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderCore<BandWidth, Hash>::remove_order_from_location(std::size_t slot) noexcept {
    const OrderLocation entry = order_location_table_[slot];

    if (entry.side == Side::Buy)
        remove_resting_at_index<Side::Buy>(entry.price, entry.resting_index);
    else
        remove_resting_at_index<Side::Sell>(entry.price, entry.resting_index);

    remove_slot_from_order_locations(slot);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
void DenseLadderCore<BandWidth, Hash>::remove_resting_at_index(
    Price price, std::uint32_t resting_index) noexcept {
    RestingOrderNode &node = resting_order_pool_[resting_index];

    auto from_overflow = [&](OverflowLevels &levels) {
        auto it = overflow_find_level<S>(levels, price);

        assert(it != levels.end() && it->first == price && "resting order's level must exist");

        remove_resting_from_level(it->second, node, resting_index);
        if (it->second.head == invalid_resting_index)
            levels.erase(it);
    };

    with_side<S>([&](auto &better_overflow, auto &ladder, auto &occupied, auto &best_slot,
                     auto &worse_overflow) {
        if (in_band(price)) {
            const std::size_t slot = price_diff_to_size_t(price, ladder_low_);
            remove_resting_from_level(ladder[slot], node, resting_index);
            if (ladder[slot].head == invalid_resting_index) {
                clear_occupied(occupied, slot);
                if (slot == best_slot) {
                    best_slot = next_worse_ladder_slot<S>(occupied, slot);
                }
            }
        } else {
            auto &levels = better_than_band<S>(price) ? better_overflow : worse_overflow;
            from_overflow(levels);
        }
    });
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderCore<BandWidth, Hash>::remove_resting_from_level(
    Level &level, RestingOrderNode &node, std::uint32_t resting_index) noexcept {

    const std::uint32_t prev = node.prev;
    const std::uint32_t next = node.next;

    level.total_quantity -= node.quantity;

    if (prev == invalid_resting_index) {
        level.head = next;
        if (next != invalid_resting_index)
            resting_order_pool_[next].prev = invalid_resting_index;
        else
            level.tail = invalid_resting_index;
    } else if (next == invalid_resting_index) {
        resting_order_pool_[prev].next = invalid_resting_index;
        level.tail = prev;
    } else {
        resting_order_pool_[prev].next = next;
        resting_order_pool_[next].prev = prev;
    }

    node.next = resting_order_pool_head_;
    resting_order_pool_head_ = resting_index;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderCore<BandWidth, Hash>::remove_slot_from_order_locations(std::size_t slot) noexcept {
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
bool DenseLadderCore<BandWidth, Hash>::remove_slot_from_order_locations(OrderId id) noexcept {
    const std::size_t slot = find_order_location(id);
    if (slot == invalid_hash_slot)
        return false;

    remove_slot_from_order_locations(slot);

    return true;
}

// rest

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
void DenseLadderCore<BandWidth, Hash>::rest_overflow(OverflowLevels &levels, Price price,
                                                     std::uint32_t resting_index,
                                                     Quantity quantity) {
    auto it = overflow_find_level<S>(levels, price);

    if (it == levels.end() || it->first != price)
        it = levels.insert(it, {price, Level{}});

    append_to_level(it->second, resting_index, quantity);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
void DenseLadderCore<BandWidth, Hash>::rest_ladder(Ladder &levels, LadderOccupancy &occupied,
                                                   std::size_t &best_slot, Price price,
                                                   std::uint32_t resting_index,
                                                   Quantity quantity) noexcept {
    assert(in_band(price) && "price must be inside the band");
    const std::size_t slot = price_diff_to_size_t(price, ladder_low_);

    append_to_level(levels[slot], resting_index, quantity);

    set_occupied(occupied, slot);

    if (best_slot == invalid_price_slot || worse<S>(best_slot, slot))
        best_slot = slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderCore<BandWidth, Hash>::append_to_level(Level &level, std::uint32_t resting_index,
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

// building blocks

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t DenseLadderCore<BandWidth, Hash>::find_order_location(OrderId id) const noexcept {
    const std::size_t slot = probe_slot(id);
    return order_location_table_[slot].resting_index == invalid_resting_index ? invalid_hash_slot
                                                                              : slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t DenseLadderCore<BandWidth, Hash>::probe_slot(OrderId id) const noexcept {
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

// ladder occupancy

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
template <Side S>
std::size_t
DenseLadderCore<BandWidth, Hash>::next_worse_ladder_slot(const LadderOccupancy &occupied,
                                                         std::size_t slot) noexcept {
    if constexpr (S == Side::Buy)
        return lower_occupied_slot(occupied, slot);
    else
        return higher_occupied_slot(occupied, slot);
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t DenseLadderCore<BandWidth, Hash>::lower_occupied_slot(const LadderOccupancy &occupied,
                                                                  std::size_t slot) noexcept {
    if (slot == 0)
        return invalid_price_slot;

    std::size_t word_index = slot >> 6;
    const std::uint64_t bit_index = static_cast<std::uint64_t>(slot & 63);

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

    return invalid_price_slot;
}

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
std::size_t DenseLadderCore<BandWidth, Hash>::higher_occupied_slot(const LadderOccupancy &occupied,
                                                                   std::size_t slot) noexcept {
    if (slot == BandWidth - 1)
        return invalid_price_slot;

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

    return invalid_price_slot;
}

} // namespace lob::books