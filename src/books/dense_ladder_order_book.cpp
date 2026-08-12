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
                                                           TradeWriter &trade_writer) {}

// private functions
template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
void DenseLadderOrderBook<BandWidth, Hash>::reserve(const Config &config) {
    order_pool.reserve(config.reserve_orders);

    base_price_ = config.base;

    std::size_t hash_slots =
        std::max<std::size_t>(2, static_cast<std::size_t>(config.reserve_orders * 2));
    hash_slots = std::bit_ceil(hash_slots);

    order_index_shift_ = 64 - std::countr_zero(hash_slots);
    order_index_.resize(hash_slots);
    order_index_mask_ = hash_slots - 1;

    assert(std::has_single_bit(order_index_.size()) &&
           "The hash map (vector) must have a power of 2 size.");
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
AddStatus
DenseLadderOrderBook<BandWidth, Hash>::validate_new_order(const NewOrder &order) const noexcept {
    std::size_t slot = find_id_entry(order.id);
    if (slot != invalid_slot) {
        return AddStatus::DuplicateOrderId;
    }

    if (order.time_in_force == TimeInForce::Fok && !can_fully_fill(order)) {
        return AddStatus::WouldNotFullyFill;
    }

    return AddStatus::Accepted;
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
const std::size_t DenseLadderOrderBook<BandWidth, Hash>::find_id_entry(OrderId id) const noexcept {
    std::size_t slot = Hash::hash_into_slot(id, order_index_mask_, )

        while (true) {
        const IdEntry &entry = order_index_[slot];

        if (entry.node_index == invalid_index)
            return invalid_slot;

        if (entry.id == id)
            return slot;

        slot = (slot + 1) & order_index_mask_;
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
std::size_t
DenseLadderOrderBook<BandWidth, Hash>::previous_occupied_slot(const auto &occupied,
                                                              std::size_t slot) noexcept {
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
std::size_t DenseLadderOrderBook<BandWidth, Hash>::next_occupied_slot(const auto &occupied,
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
template <Side OppositeSide, bool ExcludeOrder, bool StpActive>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(const NewOrder &order,
                                                            OrderId excluded_id) const noexcept {
    Quantity remaining = order.quantity;
    const std::size_t limit_slot = static_cast<std::size_t>(order.price - base_price_);

    if constexpr (OppositeSide == Side::Buy) {
        std::size_t slot = best_bid_slot_;

        std::uint32_t excluded_node = invalid_index;
        std::size_t excluded_slot = invalid_index;
        Quantity excluded_quantity{};

        if constexpr (ExcludeOrder) {
            const IdEntry &entry = order_index_[find_id_entry(excluded_id)];

            if (entry.side == OppositeSide) {
                excluded_node = entry.node_index;
                excluded_slot = static_cast<std::size_t>(entry.price - base_price_);
                excluded_quantity = order_pool_[entry.node_index].quantity;
            }
        }

        if constexpr (StpActive) {
            const StpId stp_id = order.stp_id;
            const SelfTradeResolve stp_policy = order.self_trade_resolve;

            while (slot != invalid_index && slot >= limit_slot) {
                std::uint32_t node = bids_[slot].head;

                while (node != invalid_index) {
                    const RestingOrderNode &resting = order_pool_[node];
                    const std::uint32_t next = resting.next;

                    if constexpr (ExcludeOrder) {
                        if (node == excluded_node) {
                            node = next;
                            continue;
                        }
                    }

                    if (resting.stp_id == stp_id) {
                        switch (stp_policy) {
                        case SelfTradeResolve::CancelNew:
                        case SelfTradeResolve::CancelBoth:
                            return false;

                        case SelfTradeResolve::CancelResting:
                            node = next;
                            continue;

                        case SelfTradeResolve::DecrementAndCancel:
                            break;
                        }
                    }

                    if (resting.quantity >= remaining)
                        return true;

                    remaining -= resting.quantity;
                    node = next;
                }

                slot = previous_occupied_slot(bids_occupied_, slot);
            }

            return false;

        } else {
            while (slot != invalid_index && slot >= limit_slot) {
                Quantity available = bids_[slot].total_quantity;

                if constexpr (ExcludeOrder) {
                    if (slot == excluded_slot)
                        available -= excluded_quantity;
                }

                if (available >= remaining)
                    return true;

                remaining -= available;
                slot = previous_occupied_slot(bids_occupied_, slot);
            }

            return false;
        }
    } else {

        std::size_t slot = best_ask_slot_;

        std::uint32_t excluded_node = invalid_index;
        std::size_t excluded_slot = invalid_index;
        Quantity excluded_quantity{};

        if constexpr (ExcludeOrder) {
            const IdEntry &entry = order_index_[find_id_entry(excluded_id)];

            if (entry.side == OppositeSide) {
                excluded_node = entry.node_index;
                excluded_slot = static_cast<std::size_t>(entry.price - base_price_);
                excluded_quantity = order_pool_[entry.node_index].quantity;
            }
        }

        if constexpr (StpActive) {
            const StpId stp_id = order.stp_id;
            const SelfTradeResolve stp_policy = order.self_trade_resolve;

            while (slot != invalid_index && slot <= limit_slot) {
                std::uint32_t node = asks_[slot].head;

                while (node != invalid_index) {
                    const RestingOrderNode &resting = order_pool_[node];
                    const std::uint32_t next = resting.next;

                    if constexpr (ExcludeOrder) {
                        if (node == excluded_node) {
                            node = next;
                            continue;
                        }
                    }

                    if (resting.stp_id == stp_id) {
                        switch (stp_policy) {
                        case SelfTradeResolve::CancelNew:
                        case SelfTradeResolve::CancelBoth:
                            return false;

                        case SelfTradeResolve::CancelResting:
                            node = next;
                            continue;

                        case SelfTradeResolve::DecrementAndCancel:
                            break;
                        }
                    }

                    if (resting.quantity >= remaining)
                        return true;

                    remaining -= resting.quantity;
                    node = next;
                }

                slot = next_occupied_slot(asks_occupied_, slot);
            }

            return false;

        } else {
            while (slot != invalid_index && slot <= limit_slot) {
                Quantity available = asks_[slot].total_quantity;

                if constexpr (ExcludeOrder) {
                    if (slot == excluded_slot)
                        available -= excluded_quantity;
                }

                if (available >= remaining)
                    return true;

                remaining -= available;
                slot = next_occupied_slot(asks_occupied_, slot);
            }

            return false;
        }
    }
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(const NewOrder &order,
                                                            OrderId excluded_id) const noexcept {
    if (order.stp_id == StpId{0}) {
        return can_fill_levels<OppositeSide, ExcludeOrder, false>(order, excluded_id);
    }

    return can_fill_levels<OppositeSide, ExcludeOrder, true>(order, excluded_id);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fully_fill(const NewOrder &order) const noexcept {
    return order.side == Side::Buy ? can_fill_levels<Side::Sell, false>(order);
               : can_fill_levels<Side::Buy, false>(order);
}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fully_fill(const NewOrder &order,
                                                           OrderId id) const noexcept {
    return order.side == Side::Buy ? can_fill_levels<Side::Sell, true>(order, id)
                                   : can_fill_levels<Side::Buy, true>(order, id);
}

} // namespace lob::books