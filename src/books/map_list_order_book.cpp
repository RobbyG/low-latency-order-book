#include <lob/books/map_list_order_book.hpp>

#include <algorithm>
#include <cassert>
#include <utility>

namespace lob::books {

namespace {

template <typename OppositeLevels>
[[nodiscard]] bool can_fill_levels(const OppositeLevels &levels, const NewOrder &order) noexcept {
    const auto comparator = levels.key_comp();
    const bool is_limit = order.order_type == OrderType::Limit;
    const bool stp_active = order.stp_id != StpId{0};

    Quantity quantity_needed = order.quantity;

    for (const auto &[price, orders] : levels) {
        if (is_limit && comparator(order.price, price)) {
            break;
        }

        for (const auto &resting_order : orders) {
            if (stp_active && resting_order.stp_id == order.stp_id) {
                switch (order.self_trade_resolve) {
                case SelfTradeResolve::CancelNew:
                case SelfTradeResolve::CancelBoth:
                    return false;

                case SelfTradeResolve::CancelResting:
                    continue;

                case SelfTradeResolve::DecrementAndCancel:
                    break;
                }
            }

            if (resting_order.quantity >= quantity_needed) {
                return true;
            }

            quantity_needed -= resting_order.quantity;
        }
    }

    return false;
}

template <typename Levels>
[[nodiscard]] CopyResult copy_depth_from(const Levels &levels, std::span<PriceLevel> out) noexcept {

    std::uint32_t copied = 0;

    for (const auto &[price, orders] : levels) {
        if (copied == out.size()) {
            break;
        }

        Quantity total_quantity{0};

        for (const auto &order : orders) {
            total_quantity += order.quantity;
        }

        out[copied++] = PriceLevel{
            .price = price,
            .quantity = total_quantity,
        };
    }

    return CopyResult{
        .copied = copied,
        .available = levels.size(),
    };
}

template <typename Levels>
[[nodiscard]] CopyResult copy_orders_from(const Levels &levels, Side side, Price price,
                                          std::span<OrderView> out) noexcept {
    std::uint32_t copied = 0;
    auto level_it = levels.find(price);

    if (level_it == levels.end()) {
        return CopyResult{
            .copied = 0,
            .available = 0,
        };
    }

    const auto &orders = level_it->second;

    for (const auto &order : orders) {
        if (copied == out.size()) {
            break;
        }

        out[copied++] = OrderView{.id = order.id,
                                  .price = price,
                                  .quantity = order.quantity,
                                  .stp_id = order.stp_id,
                                  .side = side,
                                  .time_in_force = order.time_in_force};
    }

    return CopyResult{
        .copied = copied,
        .available = orders.size(),
    };
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
        return AddResult{.remaining = order.quantity,
                         .trade_count = 0,
                         .status = status,
                         .outcome = AddOutcome::None};
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
        return ReplaceResult{.remaining = Quantity{0},
                             .status = ReplaceStatus::NotFound,
                             .outcome = AddOutcome::None};
    }

    const AddStatus status = validate_new_replace_order(order, id);
    if (status != AddStatus::Accepted) {
        return ReplaceResult{.remaining = index_it->second.it->quantity,
                             .trade_count = 0,
                             .status = lob::detail::to_replace_status(status),
                             .outcome = AddOutcome::None};
    }

    erase_resting(index_it);
    AddResult result = add_validated_order(order, trade_writer);
    return ReplaceResult{.remaining = result.remaining,
                         .trade_count = result.trade_count,
                         .status = lob::detail::to_replace_status(result.status),
                         .outcome = result.outcome};
}

bool MapListOrderBook::best_bid(PriceQuantity &pq) const noexcept {
    if (bids_.empty())
        return false;

    const auto &[price, orders] = *bids_.begin();

    Quantity total{};
    for (const RestingOrder &order : orders)
        total += order.quantity;

    pq = PriceQuantity{.price = price, .quantity = total};
    return true;
}

bool MapListOrderBook::best_ask(PriceQuantity &pq) const noexcept {
    if (asks_.empty())
        return false;

    const auto &[price, orders] = *asks_.begin();

    Quantity total{};
    for (const RestingOrder &order : orders)
        total += order.quantity;

    pq = PriceQuantity{.price = price, .quantity = total};
    return true;
}

bool MapListOrderBook::best_price_quantity(Side side, PriceQuantity &pq) const noexcept {
    return side == Side::Buy ? best_bid(pq) : best_ask(pq);
}

bool MapListOrderBook::empty() const noexcept {
    return order_index_.empty();
}

std::uint32_t MapListOrderBook::order_count() const noexcept {
    return static_cast<std::uint32_t>(order_index_.size());
}

std::uint32_t MapListOrderBook::price_level_count(Side side) const noexcept {
    if (side == Side::Buy) {
        return static_cast<std::uint32_t>(bids_.size());
    } else {
        return static_cast<std::uint32_t>(asks_.size());
    }
}

CopyResult MapListOrderBook::copy_bid_depth(std::span<PriceLevel> out) const noexcept {
    return copy_depth_from(bids_, out);
}

CopyResult MapListOrderBook::copy_ask_depth(std::span<PriceLevel> out) const noexcept {
    return copy_depth_from(asks_, out);
}

CopyResult MapListOrderBook::copy_depth(Side side, std::span<PriceLevel> out) const noexcept {

    switch (side) {
    case Side::Buy:
        return copy_bid_depth(out);
    case Side::Sell:
        return copy_ask_depth(out);
    }

    std::unreachable();
}

bool MapListOrderBook::find_order(OrderId id, OrderView &out) const noexcept {
    auto order_it = order_index_.find(id);
    if (order_it == order_index_.end()) {
        return false;
    }

    const OrderLocation &location = order_it->second;
    out = OrderView{.id = location.it->id,
                    .price = location.price,
                    .quantity = location.it->quantity,
                    .stp_id = location.it->stp_id,
                    .side = location.side,
                    .time_in_force = location.it->time_in_force};
    return true;
}

CopyResult MapListOrderBook::copy_best_bid_orders(std::span<OrderView> out) const noexcept {
    return copy_orders_from(bids_, Side::Buy, bids_.begin()->first, out);
}
CopyResult MapListOrderBook::copy_best_ask_orders(std::span<OrderView> out) const noexcept {
    return copy_orders_from(asks_, Side::Sell, asks_.begin()->first, out);
}
CopyResult MapListOrderBook::copy_orders_at_price(Side side, Price price,
                                                  std::span<OrderView> out) const noexcept {
    switch (side) {
    case Side::Buy:
        return copy_orders_from(bids_, Side::Buy, price, out);
    case Side::Sell:
        return copy_orders_from(asks_, Side::Sell, price, out);
    }

    std::unreachable();
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

AddStatus MapListOrderBook::validate_new_replace_order(const NewOrder &order,
                                                       OrderId id) const noexcept {
    if (order_index_.contains(order.id) && order.id != id) {
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
        return match_and_add(asks_, bids_, order, trade_writer);
    } else {
        return match_and_add(bids_, asks_, order, trade_writer);
    }
}

template <typename OppositeLevels, typename SameSideLevels>
AddResult MapListOrderBook::match_and_add(OppositeLevels &opposite_levels,
                                          SameSideLevels &same_side_levels, const NewOrder &order,
                                          TradeWriter &trade_writer) {

    const auto comparator = opposite_levels.key_comp();
    auto level_it = opposite_levels.begin();
    Quantity remaining = order.quantity;
    std::uint32_t trade_count = 0;

    const OrderId aggressive_id = order.id;
    const Price aggressive_price = order.price;
    const Side aggressive_side = order.side;
    const StpId aggressive_stp_id = order.stp_id;
    const SelfTradeResolve stp_policy = order.self_trade_resolve;

    const bool stp_active = (aggressive_stp_id != StpId{0});
    const OrderType aggressive_order_type = order.order_type;
    const TimeInForce aggressive_time_in_force = order.time_in_force;
    const bool is_limit = aggressive_order_type == OrderType::Limit;
    bool stp_decrement_and_cancel_hit = false;

    while (level_it != opposite_levels.end() && remaining != Quantity{0} &&
           (!is_limit || !comparator(aggressive_price, level_it->first))) {
        // matching
        auto &resting_orders = level_it->second;
        auto resting_it = resting_orders.begin();
        const Price level_price = level_it->first;

        while (resting_it != resting_orders.end() && remaining != Quantity{0}) {

            bool emit_trade = true;
            // perform stp check
            if (aggressive_stp_id == resting_it->stp_id &&
                stp_active) { // chose this order assuming more stps are set than not
                switch (stp_policy) {

                case SelfTradeResolve::CancelBoth:
                    order_index_.erase(resting_it->id);
                    resting_orders.erase(resting_it);

                    if (resting_orders.empty()) {
                        opposite_levels.erase(level_it);
                    }

                    return AddResult{.remaining = remaining,
                                     .trade_count = trade_count,
                                     .status = AddStatus::Accepted,
                                     .outcome = AddOutcome::STPCancelBoth};

                case SelfTradeResolve::CancelNew:
                    return AddResult{.remaining = remaining,
                                     .trade_count = trade_count,
                                     .status = AddStatus::Accepted,
                                     .outcome = AddOutcome::STPCancelNew};

                case SelfTradeResolve::DecrementAndCancel:
                    emit_trade = false;
                    break;

                case SelfTradeResolve::CancelResting:
                    order_index_.erase(resting_it->id);
                    resting_it = resting_orders.erase(resting_it);
                    continue;
                }
            }

            const Quantity trade_quantity = std::min(remaining, resting_it->quantity);

            if (!emit_trade)
                stp_decrement_and_cancel_hit = true;
            else {
                const Trade trade{.aggressive_order_id = aggressive_id,
                                  .resting_order_id = resting_it->id,
                                  .price = level_price,
                                  .quantity = trade_quantity,
                                  .aggressive_side = aggressive_side};
                trade_writer.on_trade(trade);
                ++trade_count;
            }
            remaining -= trade_quantity;
            resting_it->quantity -= trade_quantity;

            if (resting_it->quantity == Quantity{0}) {
                order_index_.erase(resting_it->id);
                resting_it = resting_orders.erase(resting_it);
            }
        }

        if (resting_orders.empty()) {
            level_it = opposite_levels.erase(level_it);
        } else {
            assert(remaining == Quantity{0});
            break;
        }
    }

    AddOutcome outcome = AddOutcome::Filled;
    if (stp_decrement_and_cancel_hit)
        outcome = AddOutcome::STPDecrementAndCancelFilled;

    if (remaining != Quantity{0}) {
        switch (aggressive_time_in_force) {
        case TimeInForce::Gtc:
        case TimeInForce::Gfd: {
            assert(is_limit);
            outcome = stp_decrement_and_cancel_hit ? AddOutcome::STPDecrementAndCancelRested
                                                   : AddOutcome::Rested;

            auto hint = same_side_levels.begin();
            const bool new_best = (hint == same_side_levels.end()) ||
                                  same_side_levels.key_comp()(aggressive_price, hint->first);

            auto rest_level_it = new_best ? same_side_levels.try_emplace(hint, aggressive_price)
                                          : same_side_levels.try_emplace(aggressive_price).first;

            auto &resting_orders = rest_level_it->second;
            auto resting_it = resting_orders.emplace(
                resting_orders.end(), RestingOrder{.id = aggressive_id,
                                                   .quantity = remaining,
                                                   .stp_id = aggressive_stp_id,
                                                   .time_in_force = aggressive_time_in_force});
            [[maybe_unused]] const bool inserted =
                order_index_
                    .emplace(aggressive_id, OrderLocation{.side = aggressive_side,
                                                          .price = aggressive_price,
                                                          .it = resting_it})
                    .second;
            assert(inserted);
            break;
        }
        case TimeInForce::Ioc:
            outcome = AddOutcome::RemainderCancelled;
            break;
        }
    }

    return AddResult{.remaining = remaining,
                     .trade_count = trade_count,
                     .status = AddStatus::Accepted,
                     .outcome = outcome};
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

template <typename OppositeLevels>
void MapListOrderBook::erase_from_level(OppositeLevels &levels,
                                        const OrderLocation &location) noexcept {
    const auto level_it = levels.find(location.price);
    assert(level_it != levels.end());

    auto &orders = level_it->second;
    orders.erase(location.it);
    if (orders.empty()) {
        levels.erase(level_it);
    }
}

} // namespace lob::books