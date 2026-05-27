#include <lob/order_book.hpp>

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

void OrderBook::add_buy_order(Order order, std::vector<Trade> &trades) {

    if (asks_.empty()) {
        bids_[order.price].push_back(order);
        order_locations_[order.id] = {Side::Buy, order.price, std::prev(bids_[order.price].end())};
    } else {
        auto ask_it = asks_.begin();

        while (ask_it != asks_.end() && ask_it->first <= order.price && order.quantity > 0) {
            auto &ask_queue = ask_it->second;

            while (!ask_queue.empty() && order.quantity > 0) {
                auto &ask_order = ask_queue.front();
                Quantity trade_quantity = std::min(order.quantity, ask_order.quantity);
                trades.push_back({order.id, ask_order.id, ask_order.price, trade_quantity});
                order.quantity -= trade_quantity;
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

        if (order.quantity > 0) {
            bids_[order.price].push_back(order);
            order_locations_[order.id] = {Side::Buy, order.price,
                                          std::prev(bids_[order.price].end())};
        }
    }
}

void OrderBook::add_sell_order(Order order, std::vector<Trade> &trades) {
    if (bids_.empty()) {
        asks_[order.price].push_back(order);
        order_locations_[order.id] = {Side::Sell, order.price, std::prev(asks_[order.price].end())};
    } else {
        auto bid_it = bids_.begin();

        while (bid_it != bids_.end() && bid_it->first >= order.price && order.quantity > 0) {
            auto &bid_queue = bid_it->second;
            while (!bid_queue.empty() && order.quantity > 0) {
                auto &bid_order = bid_queue.front();
                Quantity trade_quantity = std::min(bid_order.quantity, order.quantity);
                trades.push_back({bid_order.id, order.id, bid_order.price, trade_quantity});
                bid_order.quantity -= trade_quantity;
                order.quantity -= trade_quantity;

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

        if (order.quantity > 0) {
            bids_[order.price].push_back(order);
            order_locations_[order.id] = {Side::Sell, order.price,
                                          std::prev(bids_[order.price].end())};
        }
    }
}

} // namespace lob
