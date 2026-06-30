#pragma once

#include <lob/domain_types.hpp>
#include <lob/order.hpp>
#include <lob/order_book_results.hpp>

#include <cstdint>
#include <span>

namespace lob {

class EventWriter;

struct OrderBookConfig {
    std::uint32_t max_orders;
    std::uint32_t max_price_levels;
};

class OrderBook final {
  public:
    explicit OrderBook(OrderBookConfig config);

    OrderBook(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &) = delete;

    OrderBook(OrderBook &&) = delete;
    OrderBook &operator=(OrderBook &&) = delete;

    void reset() noexcept;

    [[nodiscard]] AddResult add_order(const NewOrder &order, EventWriter &events) noexcept;

    /*
    [[nodiscard]] CancelResult cancel_order(OrderId id) noexcept;
    [[nodiscard]] ReduceResult reduce_order_by(OrderId id, Quantity quantity) noexcept;
    [[nodiscard]] ReplaceResult replace_order(OrderId id, const NewOrder& order,
                                              EventWriter& events) noexcept;

    [[nodiscard]] bool best_bid(BestOrder& out) const noexcept;
    [[nodiscard]] bool best_ask(BestOrder& out) const noexcept;
    [[nodiscard]] bool best_order(Side side, BestOrder& out) const noexcept;

    [[nodiscard]] CopyResult copy_bid_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_ask_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_depth(Side side, std::span<PriceLevel> out) const noexcept;

    [[nodiscard]] bool find_order(OrderId id, OrderView& out) const noexcept;
    [[nodiscard]] CopyResult copy_best_bid_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_best_ask_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_orders_at_price(Side side, Price price,
                                                  std::span<OrderView> out) const noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::uint32_t order_count() const noexcept;
    [[nodiscard]] std::uint32_t price_level_count(Side side) const noexcept;
    */

#ifndef NDEBUG
    void validate() const;
#endif

  private:
    void reserve(OrderBookConfig config);

    [[nodiscard]] AddStatus validate_new_order(const NewOrder &order) const noexcept;
    [[nodiscard]] AddResult add_validated_order(const NewOrder &order,
                                                EventWriter &events) noexcept;
};

} // namespace lob