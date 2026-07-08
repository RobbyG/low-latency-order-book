#include <lob/books/map_list_order_book.hpp>

#include <cassert>

namespace lob::books {

// construction
MapListOrderBook::MapListOrderBook(Config config) : config_(config) {
    reserve(config_);
}

// core mutation api

AddResult MapListOrderBook::add_order(const NewOrder &new_order, TradeWriter &trade_writer) {
    const AddStatus status = validate_new_order(new_order);
    if (status != AddStatus::Accepted) {
        return AddResult{.remaining = new_order.quantity, .trade_count = 0, .status = status};
    }

    return add_validated_order(new_order, trade_writer);
}

CancelResult MapListOrderBook::cancel_order(OrderId id) noexcept {
    auto index_it = order_index_.find(id);

    if (index_it == order_index_.end()) {
        // order does not exist
        return CancelResult{.quantity = Quantity{}, .status = CancelStatus::NotFound};
    }

    // order exists
    const CancelResult result{.quantity = index_it->second.it->quantity,
                              .status = CancelStatus::Cancelled};
    erase_resting(index_it);
    return result;
}

ReduceResult MapListOrderBook::reduce_order_by(OrderId id, Quantity quantity) noexcept {
    auto index_it = order_index_.find(id);

    if (index_it == order_index_.end()) {
        // order does not exist
        return ReduceResult{.old_quantity = Quantity{0},
                            .new_quantity = Quantity{0},
                            .status = ReduceStatus::NotFound};
    }

    const OrderLocation &location = index_it->second;
    const Quantity resting = location.it->quantity;

    if (resting < quantity) {
        // quantity with which to reduce is larger than resting quantity
        return ReduceResult{.old_quantity = resting,
                            .new_quantity = resting,
                            .status = ReduceStatus::InvalidQuantity};
    }

    if (resting == quantity) {
        erase_resting(index_it);
        return ReduceResult{.old_quantity = resting,
                            .new_quantity = Quantity{0},
                            .status = ReduceStatus::Cancelled};
    }
    // valid quantity, reducing
    location.it->quantity -= quantity;
    return ReduceResult{.old_quantity = resting,
                        .new_quantity = resting - quantity,
                        .status = ReduceStatus::Reduced};
}

// private helpers

void MapListOrderBook::reserve(const Config &config) {
    order_index_.reserve(config.max_orders);
}

template <typename Levels>
void MapListOrderBook::erase_from_level(Levels &levels, const OrderLocation &location) noexcept {
    const auto level_it = levels.find(location.price);
    assert(level_it != levels.end());

    auto &orders = level_it->second;
    orders.erase(location.it);
    if (orders.empty()) {
        levels.erase(level_it);
    }
}

void MapListOrderBook::erase_resting(OrderIndex::iterator index_it) noexcept {
    const OrderLocation &location = index_it->second;

    if (location.side == Side::Buy) {
        erase_from_level(bids_, location);
    } else {
        erase_from_level(asks_, location);
    }

    order_index_.erase(index_it);
}

} // namespace lob::books