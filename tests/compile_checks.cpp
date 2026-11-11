#include <lob/books/map_list_ob.hpp>
#include <lob/books/ob_concepts.hpp>

static_assert(lob::books::OrderBookConstructible<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookCommands<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookL1View<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookL2View<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookL3View<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookStats<lob::books::MapListOrderBook>);
static_assert(lob::books::OrderBookImpl<lob::books::MapListOrderBook>);