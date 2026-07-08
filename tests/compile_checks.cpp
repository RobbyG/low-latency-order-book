#include <lob/books/map_list_order_book.hpp>
#include <lob/books/order_book_concept.hpp>

static_assert(lob::books::OrderBookConstructible<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookCore<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookL1View<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookL2View<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookL3View<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookStats<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookImpl<lob::books::MapListOrderBook>);