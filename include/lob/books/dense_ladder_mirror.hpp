#pragma once

#include <lob/books/dense_ladder_core.hpp>
#include <lob/domain_types.hpp>
#include <lob/hashing/order_id_hash.hpp>
#include <lob/market_event.hpp>
#include <lob/order.hpp>
#include <lob/order_book_results.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace lob {

namespace books {

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
class DenseLadderMirror final : public DenseLadderCore<BandWidth, Hash> {
    using Core = DenseLadderCore<BandWidth, Hash>;

  public: // construction
    using typename Core::Config;
    explicit DenseLadderMirror(Config config) : Core(config) {}

    // copy and move disabled
    DenseLadderMirror(const DenseLadderMirror &) = delete;
    DenseLadderMirror &operator=(const DenseLadderMirror &) = delete;

    DenseLadderMirror(DenseLadderMirror &&) = delete;
    DenseLadderMirror &operator=(DenseLadderMirror &&) = delete;

  public: // views taken from the core
    using Core::best_ask;
    using Core::best_bid;
    using Core::best_price_quantity;
    using Core::copy_ask_depth;
    using Core::copy_best_ask_orders;
    using Core::copy_best_bid_orders;
    using Core::copy_bid_depth;
    using Core::copy_depth;
    using Core::copy_orders_at_price;
    using Core::find_order;

  public: // API
    void apply_event(const MarketEvent &event) noexcept;

  private: // names from the core
};

} // namespace books
} // namespace lob

#include <lob/books/detail/dense_ladder_mirror_impl.hpp>