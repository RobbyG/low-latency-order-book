#pragma once

#include <lob/domain_types.hpp>
#include <lob/order.hpp>
#include <lob/order_book_results.hpp>

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <span>
#include <unordered_map>

namespace lob {

class TradeWriter;

namespace books {

class MapListOrderBook final {
  public:
    struct Config {
        std::uint32_t max_orders{};
    };
    explicit MapListOrderBook(Config config);

    MapListOrderBook(const MapListOrderBook &) = delete;
    MapListOrderBook &operator=(const MapListOrderBook &) = delete;

    MapListOrderBook(MapListOrderBook &&) = delete;
    MapListOrderBook &operator=(MapListOrderBook &&) = delete;

    [[nodiscard]] AddResult add_order(const NewOrder &order, TradeWriter &trade_writer);
    [[nodiscard]] CancelResult cancel_order(OrderId id) noexcept;
    [[nodiscard]] ReduceResult reduce_order_by(OrderId id, Quantity quantity) noexcept;
    [[nodiscard]] ReplaceResult replace_order(OrderId id, const NewOrder &order,
                                              TradeWriter &trade_writer);

    [[nodiscard]] bool best_bid(Price &price) const noexcept;
    [[nodiscard]] bool best_ask(Price &price) const noexcept;
    [[nodiscard]] bool best_order(Side side, Price &price) const noexcept;

    [[nodiscard]] CopyResult copy_bid_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_ask_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_depth(Side side, std::span<PriceLevel> out) const noexcept;

    [[nodiscard]] bool find_order(OrderId id, OrderView &out) const noexcept;
    [[nodiscard]] CopyResult copy_best_bid_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_best_ask_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_orders_at_price(Side side, Price price,
                                                  std::span<OrderView> out) const noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::uint32_t order_count() const noexcept;
    [[nodiscard]] std::uint32_t price_level_count(Side side) const noexcept;

    void reset() noexcept;

  private:
    using OrderList = std::list<RestingOrder>;
    using BidLevels = std::map<Price, OrderList, std::greater<Price>>;
    using AskLevels = std::map<Price, OrderList, std::less<Price>>;

    struct OrderLocation {
        Side side;
        Price price;
        OrderList::iterator it;
    };

    using OrderIndex = std::unordered_map<OrderId, OrderLocation>;

    void reserve(const Config &config);

    [[nodiscard]] AddStatus validate_new_order(const NewOrder &order) const noexcept;
    [[nodiscard]] AddResult add_validated_order(const NewOrder &order, TradeWriter &trade_writer);
    template <typename Levels>
    [[nodiscard]] AddResult match_and_add(Levels &levels, const NewOrder &order,
                                          TradeWriter &trade_writer);

    [[nodiscard]] bool can_fully_fill(const NewOrder &order) const noexcept;
    void erase_resting(OrderIndex::iterator index_it) noexcept;
    template <typename Levels>
    void erase_from_level(Levels &levels, const OrderLocation &location) noexcept;

    Config config_;

    BidLevels bids_;
    AskLevels asks_;

    OrderIndex order_index_;
};

} // namespace books
} // namespace lob