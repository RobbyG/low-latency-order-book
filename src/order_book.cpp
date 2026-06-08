#include <lob/order_book.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>

namespace lob {

std::vector<Trade> OrderBook::add_order(Order order) {
    std::vector<Trade> trades;

    if (order.side == Side::Buy) {
        add_buy_order(order, trades);
    } else {
        add_sell_order(order, trades);
    }

    return trades;
}

bool OrderBook::cancel_order(OrderId order_id) {
    auto location_it = order_locations_.find(order_id);
    if (location_it == order_locations_.end())
        return false;

    const OrderLocation &location = location_it->second;

    if (location.side == Side::Buy) {
        auto bids_it = bids_.find(location.price);
        assert(bids_it != bids_.end() && "Order location point to a price that is missing");

        bids_it->second.erase(location.iterator);
        if (bids_it->second.empty())
            bids_.erase(bids_it);
    } else {
        auto asks_it = asks_.find(location.price);
        assert(asks_it != asks_.end() && "Order location point to a price that is missing");

        asks_it->second.erase(location.iterator);
        if (asks_it->second.empty())
            asks_.erase(asks_it);
    }

    order_locations_.erase(location_it);

    return true;
}

std::optional<Price> OrderBook::best_bid_price() const {
    if (bids_.empty()) {
        return std::nullopt;
    }

    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask_price() const {
    if (asks_.empty()) {
        return std::nullopt;
    }

    return asks_.begin()->first;
}

std::optional<Quantity> OrderBook::best_bid_quantity() const {
    if (bids_.empty()) {
        return std::nullopt;
    }

    const auto &best_bid_queue = bids_.begin()->second;

    Quantity total = 0;

    for (const auto &order : best_bid_queue)
        total += order.quantity;

    return total;
}

std::optional<Quantity> OrderBook::best_ask_quantity() const {
    if (asks_.empty()) {
        return std::nullopt;
    }

    const auto &best_ask_queue = asks_.begin()->second;

    Quantity total = 0;

    for (const auto &order : best_ask_queue) {
        total += order.quantity;
    }

    return total;
}

void OrderBook::add_buy_order(Order &bid_order, std::vector<Trade> &trades) {

    auto ask_it = asks_.begin();

    while (ask_it != asks_.end() && ask_it->first <= bid_order.price && bid_order.quantity > 0) {
        auto &ask_queue = ask_it->second;

        while (!ask_queue.empty() && bid_order.quantity > 0) {
            auto &ask_order = ask_queue.front();

            Quantity trade_quantity = std::min(bid_order.quantity, ask_order.quantity);
            trades.push_back({bid_order.id, ask_order.id, ask_order.price, trade_quantity});
            bid_order.quantity -= trade_quantity;
            ask_order.quantity -= trade_quantity;

            if (ask_order.quantity == 0) {
                order_locations_.erase(ask_order.id);
                ask_queue.pop_front();
            }
        }
        if (ask_queue.empty()) {
            ask_it = asks_.erase(ask_it);
        } else
            break;
    }

    if (bid_order.quantity > 0) {
        add_resting_order(bid_order);
    }
}

void OrderBook::add_sell_order(Order &ask_order, std::vector<Trade> &trades) {

    auto bid_it = bids_.begin();

    while (bid_it != bids_.end() && bid_it->first >= ask_order.price && ask_order.quantity > 0) {
        auto &bid_queue = bid_it->second;

        while (!bid_queue.empty() && ask_order.quantity > 0) {
            auto &bid_order = bid_queue.front();

            Quantity trade_quantity = std::min(bid_order.quantity, ask_order.quantity);
            trades.push_back({bid_order.id, ask_order.id, bid_order.price, trade_quantity});
            bid_order.quantity -= trade_quantity;
            ask_order.quantity -= trade_quantity;

            if (bid_order.quantity == 0) {
                order_locations_.erase(bid_order.id);
                bid_queue.pop_front();
            } else
                break;
        }

        if (bid_queue.empty()) {
            bid_it = bids_.erase(bid_it);
        } else
            break;
    }

    if (ask_order.quantity > 0) {
        add_resting_order(ask_order);
    }
}

void OrderBook::add_resting_order(const Order &order) {
    assert(order.quantity > 0 && "Cannot add a 0 quantity order into the order book");
    assert(order_locations_.find(order.id) == order_locations_.end() &&
           "Order with the same id already exists in the order book");

    if (order.side == Side::Buy) {
        auto &bid_queue = bids_[order.price];
        bid_queue.push_back(order);
        order_locations_[order.id] = {Side::Buy, order.price, std::prev(bid_queue.end())};
    } else {
        auto &ask_queue = asks_[order.price];
        ask_queue.push_back(order);
        order_locations_[order.id] = {Side::Sell, order.price, std::prev(ask_queue.end())};
    }
}

} // namespace lob
