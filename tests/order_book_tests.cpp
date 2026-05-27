#include <lob/order_book.hpp>

#include <cassert>
#include <iostream>
#include <optional>
#include <vector>

namespace {

void expect_price(const std::optional<lob::Price> &actual, lob::Price expected) {
    assert(actual.has_value());
    assert(*actual == expected);
}

void expect_quantity(const std::optional<lob::Quantity> &actual, lob::Quantity expected) {
    assert(actual.has_value());
    assert(*actual == expected);
}

void expect_no_price(const std::optional<lob::Price> &actual) {
    assert(!actual.has_value());
}

void expect_no_quantity(const std::optional<lob::Quantity> &actual) {
    assert(!actual.has_value());
}

void expect_trade(const lob::Trade &trade, lob::OrderId expected_buy_id,
                  lob::OrderId expected_sell_id, lob::Price expected_price,
                  lob::Quantity expected_quantity) {
    assert(trade.buy_order_id == expected_buy_id);
    assert(trade.sell_order_id == expected_sell_id);
    assert(trade.price == expected_price);
    assert(trade.quantity == expected_quantity);
}

void test_empty_book_has_no_best_prices() {
    lob::OrderBook book;

    expect_no_price(book.best_bid_price());
    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_bid_quantity());
    expect_no_quantity(book.best_ask_quantity());
}

void test_buy_order_rests_as_best_bid() {
    lob::OrderBook book;

    auto trades = book.add_order(lob::Order{1, 100, 10, lob::Side::Buy});

    assert(trades.empty());
    expect_price(book.best_bid_price(), 100);
    expect_quantity(book.best_bid_quantity(), 10);
    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

void test_sell_order_rests_as_best_ask() {
    lob::OrderBook book;

    auto trades = book.add_order(lob::Order{1, 105, 7, lob::Side::Sell});

    assert(trades.empty());
    expect_price(book.best_ask_price(), 105);
    expect_quantity(book.best_ask_quantity(), 7);
    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
}

void test_non_crossing_orders_both_rest() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 10, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{2, 105, 7, lob::Side::Sell}).empty());

    expect_price(book.best_bid_price(), 100);
    expect_quantity(book.best_bid_quantity(), 10);

    expect_price(book.best_ask_price(), 105);
    expect_quantity(book.best_ask_quantity(), 7);
}

void test_buy_hits_resting_ask_partial_fill() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 10, lob::Side::Sell}).empty());

    auto trades = book.add_order(lob::Order{2, 105, 4, lob::Side::Buy});

    assert(trades.size() == 1);
    expect_trade(trades[0], 2, 1, 100, 4);

    expect_price(book.best_ask_price(), 100);
    expect_quantity(book.best_ask_quantity(), 6);

    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
}

void test_sell_hits_resting_bid_partial_fill() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 10, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{2, 95, 4, lob::Side::Sell});

    assert(trades.size() == 1);
    expect_trade(trades[0], 1, 2, 100, 4);

    expect_price(book.best_bid_price(), 100);
    expect_quantity(book.best_bid_quantity(), 6);

    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

void test_buy_consumes_ask_and_remainder_rests_as_bid() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 4, lob::Side::Sell}).empty());

    auto trades = book.add_order(lob::Order{2, 105, 10, lob::Side::Buy});

    assert(trades.size() == 1);
    expect_trade(trades[0], 2, 1, 100, 4);

    expect_price(book.best_bid_price(), 105);
    expect_quantity(book.best_bid_quantity(), 6);

    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

void test_sell_consumes_bid_and_remainder_rests_as_ask() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 4, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{2, 95, 10, lob::Side::Sell});

    assert(trades.size() == 1);
    expect_trade(trades[0], 1, 2, 100, 4);

    expect_price(book.best_ask_price(), 95);
    expect_quantity(book.best_ask_quantity(), 6);

    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
}

void test_exact_full_fill_leaves_book_empty() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{2, 100, 5, lob::Side::Sell});

    assert(trades.size() == 1);
    expect_trade(trades[0], 1, 2, 100, 5);

    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

void test_best_bid_price_priority() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 10, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{2, 101, 10, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{3, 99, 5, lob::Side::Sell});

    assert(trades.size() == 1);
    expect_trade(trades[0], 2, 3, 101, 5);

    expect_price(book.best_bid_price(), 101);
    expect_quantity(book.best_bid_quantity(), 5);
}

void test_best_ask_price_priority() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 105, 10, lob::Side::Sell}).empty());
    assert(book.add_order(lob::Order{2, 103, 10, lob::Side::Sell}).empty());

    auto trades = book.add_order(lob::Order{3, 110, 5, lob::Side::Buy});

    assert(trades.size() == 1);
    expect_trade(trades[0], 3, 2, 103, 5);

    expect_price(book.best_ask_price(), 103);
    expect_quantity(book.best_ask_quantity(), 5);
}

void test_fifo_for_bids_same_price() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{2, 100, 5, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{3, 99, 8, lob::Side::Sell});

    assert(trades.size() == 2);
    expect_trade(trades[0], 1, 3, 100, 5);
    expect_trade(trades[1], 2, 3, 100, 3);

    expect_price(book.best_bid_price(), 100);
    expect_quantity(book.best_bid_quantity(), 2);
}

void test_fifo_for_asks_same_price() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Sell}).empty());
    assert(book.add_order(lob::Order{2, 100, 5, lob::Side::Sell}).empty());

    auto trades = book.add_order(lob::Order{3, 101, 8, lob::Side::Buy});

    assert(trades.size() == 2);
    expect_trade(trades[0], 3, 1, 100, 5);
    expect_trade(trades[1], 3, 2, 100, 3);

    expect_price(book.best_ask_price(), 100);
    expect_quantity(book.best_ask_quantity(), 2);
}

void test_buy_sweeps_multiple_ask_levels() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Sell}).empty());
    assert(book.add_order(lob::Order{2, 101, 6, lob::Side::Sell}).empty());
    assert(book.add_order(lob::Order{3, 103, 7, lob::Side::Sell}).empty());

    auto trades = book.add_order(lob::Order{4, 102, 20, lob::Side::Buy});

    assert(trades.size() == 2);
    expect_trade(trades[0], 4, 1, 100, 5);
    expect_trade(trades[1], 4, 2, 101, 6);

    expect_price(book.best_bid_price(), 102);
    expect_quantity(book.best_bid_quantity(), 9);

    expect_price(book.best_ask_price(), 103);
    expect_quantity(book.best_ask_quantity(), 7);
}

void test_sell_sweeps_multiple_bid_levels() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{2, 99, 6, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{3, 97, 7, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{4, 98, 20, lob::Side::Sell});

    assert(trades.size() == 2);
    expect_trade(trades[0], 1, 4, 100, 5);
    expect_trade(trades[1], 2, 4, 99, 6);

    expect_price(book.best_bid_price(), 97);
    expect_quantity(book.best_bid_quantity(), 7);

    expect_price(book.best_ask_price(), 98);
    expect_quantity(book.best_ask_quantity(), 9);
}

void test_best_quantity_is_total_at_best_price_level() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{2, 100, 6, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{3, 99, 100, lob::Side::Buy}).empty());

    assert(book.add_order(lob::Order{4, 105, 7, lob::Side::Sell}).empty());
    assert(book.add_order(lob::Order{5, 105, 8, lob::Side::Sell}).empty());
    assert(book.add_order(lob::Order{6, 106, 100, lob::Side::Sell}).empty());

    expect_price(book.best_bid_price(), 100);
    expect_quantity(book.best_bid_quantity(), 11);

    expect_price(book.best_ask_price(), 105);
    expect_quantity(book.best_ask_quantity(), 15);
}

void test_cancel_unknown_order_returns_false() {
    lob::OrderBook book;

    assert(!book.cancel_order(999));
}

void test_cancel_existing_bid() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 10, lob::Side::Buy}).empty());

    assert(book.cancel_order(1));

    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
}

void test_cancel_existing_ask() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 105, 10, lob::Side::Sell}).empty());

    assert(book.cancel_order(1));

    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

void test_cancel_one_bid_keeps_other_fifo_order() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Buy}).empty());
    assert(book.add_order(lob::Order{2, 100, 6, lob::Side::Buy}).empty());

    assert(book.cancel_order(1));

    expect_price(book.best_bid_price(), 100);
    expect_quantity(book.best_bid_quantity(), 6);

    auto trades = book.add_order(lob::Order{3, 99, 6, lob::Side::Sell});

    assert(trades.size() == 1);
    expect_trade(trades[0], 2, 3, 100, 6);

    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
}

void test_cancel_one_ask_keeps_other_fifo_order() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Sell}).empty());
    assert(book.add_order(lob::Order{2, 100, 6, lob::Side::Sell}).empty());

    assert(book.cancel_order(1));

    expect_price(book.best_ask_price(), 100);
    expect_quantity(book.best_ask_quantity(), 6);

    auto trades = book.add_order(lob::Order{3, 101, 6, lob::Side::Buy});

    assert(trades.size() == 1);
    expect_trade(trades[0], 3, 2, 100, 6);

    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

void test_cancel_partially_filled_bid() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 10, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{2, 99, 4, lob::Side::Sell});

    assert(trades.size() == 1);
    expect_trade(trades[0], 1, 2, 100, 4);

    expect_price(book.best_bid_price(), 100);
    expect_quantity(book.best_bid_quantity(), 6);

    assert(book.cancel_order(1));

    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
}

void test_cancel_partially_filled_ask() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 10, lob::Side::Sell}).empty());

    auto trades = book.add_order(lob::Order{2, 101, 4, lob::Side::Buy});

    assert(trades.size() == 1);
    expect_trade(trades[0], 2, 1, 100, 4);

    expect_price(book.best_ask_price(), 100);
    expect_quantity(book.best_ask_quantity(), 6);

    assert(book.cancel_order(1));

    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

void test_cancel_fully_filled_order_returns_false() {
    lob::OrderBook book;

    assert(book.add_order(lob::Order{1, 100, 5, lob::Side::Buy}).empty());

    auto trades = book.add_order(lob::Order{2, 99, 5, lob::Side::Sell});

    assert(trades.size() == 1);
    expect_trade(trades[0], 1, 2, 100, 5);

    assert(!book.cancel_order(1));
    assert(!book.cancel_order(2));

    expect_no_price(book.best_bid_price());
    expect_no_quantity(book.best_bid_quantity());
    expect_no_price(book.best_ask_price());
    expect_no_quantity(book.best_ask_quantity());
}

} // namespace

#define RUN_TEST(test_name)                                                                        \
    do {                                                                                           \
        test_name();                                                                               \
        std::cout << #test_name << " passed\n";                                                    \
    } while (false)

int main() {
    RUN_TEST(test_empty_book_has_no_best_prices);
    RUN_TEST(test_buy_order_rests_as_best_bid);
    RUN_TEST(test_sell_order_rests_as_best_ask);
    RUN_TEST(test_non_crossing_orders_both_rest);

    RUN_TEST(test_buy_hits_resting_ask_partial_fill);
    RUN_TEST(test_sell_hits_resting_bid_partial_fill);
    RUN_TEST(test_buy_consumes_ask_and_remainder_rests_as_bid);
    RUN_TEST(test_sell_consumes_bid_and_remainder_rests_as_ask);
    RUN_TEST(test_exact_full_fill_leaves_book_empty);

    RUN_TEST(test_best_bid_price_priority);
    RUN_TEST(test_best_ask_price_priority);
    RUN_TEST(test_fifo_for_bids_same_price);
    RUN_TEST(test_fifo_for_asks_same_price);
    RUN_TEST(test_buy_sweeps_multiple_ask_levels);
    RUN_TEST(test_sell_sweeps_multiple_bid_levels);
    RUN_TEST(test_best_quantity_is_total_at_best_price_level);

    RUN_TEST(test_cancel_unknown_order_returns_false);
    RUN_TEST(test_cancel_existing_bid);
    RUN_TEST(test_cancel_existing_ask);
    RUN_TEST(test_cancel_one_bid_keeps_other_fifo_order);
    RUN_TEST(test_cancel_one_ask_keeps_other_fifo_order);
    RUN_TEST(test_cancel_partially_filled_bid);
    RUN_TEST(test_cancel_partially_filled_ask);
    RUN_TEST(test_cancel_fully_filled_order_returns_false);

    std::cout << "All order book tests passed.\n";
    return 0;
}