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
                                                                      std::size_t slot) noexcept {}

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
template <Side OppositeSide, bool ExcludeOrder>
bool DenseLadderOrderBook<BandWidth, Hash>::can_fill_levels(
    const NewOrder &order, OrderId excluded_id = {}) const noexcept {

    Quantity remaining = order.quantity;

    if (OppositeSide == Side::Buy) {
        std::size_t slot = best_bid_slot_;

        while (remaining > 0 && slot >= static_cast<std::size_t>(order.price) &&
               slot != invalid_index) {
            if (bids_[slot].total_quantity >= remaining)
                return true;

            remaining -= bids_[slot].total_quantity;
        }

        if (remaining > 0)
            if (slot == invalid_index) {
                for (const auto &level : bids_overflow_) {
                    if (level.first < order.price)
                        return false;
                    remaining -= level.second;

                    if (remaining <= 0)
                        return true;
                }
            }
        return true;
    }
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