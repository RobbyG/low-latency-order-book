#include <lob/books/map_list_order_book.hpp>

#include <cassert>

namespace lob::books {

namespace {

template <typename Levels>
bool can_fill_levels(const Levels &levels, const NewOrder &order) noexcept {
    const auto comparator = levels.key_comp();
    Quantity quantity_needed = order.quantity;

    for (const auto &[price, orders] : levels) {
        if (comparator(price, order.price))
            break;

        for (const auto &resting_order : orders) {
            if (resting_order.stp_id == order.stp_id &&
                order.self_trade_resolve != SelfTradeResolve::DecrementAndCancel)
                continue;
            if (resting_order.quantity >= quantity_needed)
                return true;

            quantity_needed -= resting_order.quantity;
        }
    }

    return false;
}

} // namespace

// construction
MapListOrderBook::MapListOrderBook(Config config) : config_(config) {
    reserve(config_);
}

// core mutation api

AddResult MapListOrderBook::add_order(const NewOrder &order, TradeWriter &trade_writer) {
    const AddStatus status = validate_new_order(order);
    if (status != AddStatus::Accepted) {
        return AddResult{.remaining = order.quantity, .trade_count = 0, .status = status};
    }

    return add_validated_order(order, trade_writer);
}

CancelResult MapListOrderBook::cancel_order(OrderId id) noexcept {
    auto index_it = order_index_.find(id);

    if (index_it == order_index_.end()) {
        // order does not exist
        return CancelResult{.quantity = Quantity{0}, .status = CancelStatus::NotFound};
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

ReplaceResult MapListOrderBook::replace_order(OrderId id, const NewOrder &order,
                                              TradeWriter &trade_writer) {
    auto index_it = order_index_.find(id);

    if (index_it == order_index_.end()) {
        // order does not exist
        return ReplaceResult{.remaining = Quantity{0}, .status = ReplaceStatus::NotFound};
    }

    const AddStatus status = validate_new_order(order);
    if (status != AddStatus::Accepted) {
        return ReplaceResult{.remaining = index_it->second.it->quantity,
                             .trade_count = 0,
                             .status = lob::detail::to_replace_status(status)};
    }

    erase_resting(index_it);
    AddResult result = add_validated_order(order, trade_writer);
    return ReplaceResult{.remaining = result.remaining,
                         .trade_count = result.trade_count,
                         .status = lob::detail::to_replace_status(result.status)};
}

// private functions

void MapListOrderBook::reserve(const Config &config) {
    order_index_.reserve(config.max_orders);
}

AddStatus MapListOrderBook::validate_new_order(const NewOrder &order) const noexcept {
    if (order_index_.contains(order.id)) {
        return AddStatus::DuplicateOrderId;
    }

    if (order.time_in_force == TimeInForce::Fok && !can_fully_fill(order)) {
        return AddStatus::WouldNotFullyFill;
    }

    return AddStatus::Accepted;
}

AddResult MapListOrderBook::add_validated_order(const NewOrder &order, TradeWriter &trade_writer) {
    Quantity remaining_quantity = order.quantity;

    if (order.side == Side::Buy) {
        for (const auto &[price, orders] : asks_) {
            if (price > order.price)
                break;
            for (const auto &resting_order : orders) {
                if (resting_order.stp_id == order.stp_id)
                    continue;
                if (resting_order.quantity >= remaining_quantity)
                    ;
            }
        }
    } else {
    }
}

template <typename Levels>
AddResult MapListOrderBook::add_into_levels(Levels &levels, const NewOrder &order,
                                            TradeWriter &trade_writer) {
    if (order.time_in_force == TimeInForce::Fok) {
        if (!can_fully_fill(order))
            return AddResult{.remaining = order.quantity,
                             .trade_count = 0,
                             .status = AddStatus::WouldNotFullyFill};
    }

    const auto comparator = levels.key_comp();
    auto level_it = levels.begin();
    Quantity remaining = order.quantity;
    std::uint32_t trade_count = 0;

    if (order.order_type == OrderType::Limit) {
        while (level_it != levels.end() && comparator(order.price, level_it->first)) {
            // matched
            auto &resting_orders = level_it->second;
            auto resting_it = resting_orders.begin();

            while (resting_it != resting_orders.end() && remaining != Quantity{0}) {
                // perform stp check

                const Quantity trade_quantity = std::min(remaining, resting_it->quantity);

                const Trade trade{.aggressive_order_id = order.id,
                                  .resting_order_id = resting_it->id,
                                  .price = level_it->first,
                                  .quantity = trade_quantity,
                                  .aggressive_side = order.side};
            }
        }
    }
}

bool MapListOrderBook::can_fully_fill(const NewOrder &order) const noexcept {
    return order.side == Side::Buy ? can_fill_levels(asks_, order) : can_fill_levels(bids_, order);
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

} // namespace lob::books