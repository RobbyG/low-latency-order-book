#pragma once

#include <lob/domain_types.hpp>
#include <lob/order.hpp>
#include <lob/order_book_results.hpp>
#include <lob/trade_writer.hpp>

#include <concepts>
#include <cstdint>
#include <span>

namespace lob::books {

template <typename Book>
concept OrderBookConstructible =
    requires { typename Book::Config; } && std::constructible_from<Book, typename Book::Config>;

template <typename Book>
concept OrderBookCore = requires(Book &book, const NewOrder &order, OrderId id, Quantity quantity,
                                 TradeWriter &trade_writer) {
    { book.add_order(order, trade_writer) } noexcept -> std::same_as<AddResult>;
    { book.cancel_order(id) } noexcept -> std::same_as<CancelResult>;
    { book.reduce_order_by(id, quantity) } noexcept -> std::same_as<ReduceResult>;
    { book.replace_order(id, order, trade_writer) } noexcept -> std::same_as<ReplaceResult>;

    { book.reset() } noexcept -> std::same_as<void>;
};

template <typename Book>
concept OrderBookL1View = requires(const Book &const_book, BestOrder &best, Side side) {
    { const_book.best_bid(best) } noexcept -> std::same_as<bool>;
    { const_book.best_ask(best) } noexcept -> std::same_as<bool>;
    { const_book.best_order(side, best) } noexcept -> std::same_as<bool>;
};

template <typename Book>
concept OrderBookL2View =
    requires(const Book &const_book, Side side, std::span<PriceLevel> price_levels) {
        { const_book.copy_bid_depth(price_levels) } noexcept -> std::same_as<CopyResult>;
        { const_book.copy_ask_depth(price_levels) } noexcept -> std::same_as<CopyResult>;
        { const_book.copy_depth(side, price_levels) } noexcept -> std::same_as<CopyResult>;
    };

template <typename Book>
concept OrderBookL3View = requires(const Book &const_book, OrderId id, Side side, Price price,
                                   OrderView &order_view, std::span<OrderView> order_views) {
    { const_book.find_order(id, order_view) } noexcept -> std::same_as<bool>;

    { const_book.copy_best_bid_orders(order_views) } noexcept -> std::same_as<CopyResult>;
    { const_book.copy_best_ask_orders(order_views) } noexcept -> std::same_as<CopyResult>;
    {
        const_book.copy_orders_at_price(side, price, order_views)
    } noexcept -> std::same_as<CopyResult>;
};

template <typename Book>
concept OrderBookStats = requires(Book &book, const Book &const_book, Side side) {
    { const_book.empty() } noexcept -> std::same_as<bool>;
    { const_book.order_count() } noexcept -> std::same_as<std::uint32_t>;
    { const_book.price_level_count(side) } noexcept -> std::same_as<std::uint32_t>;
};

template <typename Book>
concept OrderBookImpl =
    OrderBookConstructible<Book> && OrderBookCore<Book> && OrderBookL1View<Book> &&
    OrderBookL2View<Book> && OrderBookL3View<Book> && OrderBookStats<Book>;
} // namespace lob::books