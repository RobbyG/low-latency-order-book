#pragma once

#include <lob/order.hpp>

#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob {

class OrderBook {

  public:
    std::vector<Trade> add_order(Order order);
    bool cancel_order(OrderId order_id);

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;

    [[nodiscard]] std::optional<Quantity> best_bid_quantity() const;
    [[nodiscard]] std::optional<Quantity> best_ask_quantity() const;

  private:
    using OrderQueue = std::list<Order>;

    using Bids = std::map<Price, OrderQueue, std::greater<Price>>;
    using Asks = std::map<Price, OrderQueue, std::less<Price>>;

    Bids bids_;
    Asks asks_;

    struct OrderLocation {
        Side side;
        Price price;
        typename OrderQueue::iterator iterator;
    };

    std::unordered_map<OrderId, OrderLocation> order_locations_;

    void add_buy_order(Order order, std::vector<Trade> &trades);
    void add_sell_order(Order order, std::vector<Trade> &trades);
};

} // namespace lob