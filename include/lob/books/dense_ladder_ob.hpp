#pragma once

#include <lob/books/dense_ladder_core.hpp>
#include <lob/domain_types.hpp>
#include <lob/hashing/order_id_hash.hpp>
#include <lob/order.hpp>
#include <lob/order_book_results.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace lob {

class EventWriter;

namespace books {

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
class DenseLadderOrderBook final : public DenseLadderCore<BandWidth, Hash> {
    using Core = DenseLadderCore<BandWidth, Hash>;

  public: // construction
    using typename Core::Config;
    explicit DenseLadderOrderBook(Config config) : Core(config) {}

    // copy and move disabled
    DenseLadderOrderBook(const DenseLadderOrderBook &) = delete;
    DenseLadderOrderBook &operator=(const DenseLadderOrderBook &) = delete;

    DenseLadderOrderBook(DenseLadderOrderBook &&) = delete;
    DenseLadderOrderBook &operator=(DenseLadderOrderBook &&) = delete;

  public: // API
    [[nodiscard]] AddResult add_order(const NewOrder &order, EventWriter &event_writer) noexcept;
    [[nodiscard]] CancelResult cancel_order(OrderId id, EventWriter &event_writer) noexcept;
    [[nodiscard]] ReduceResult reduce_order_by(OrderId id, Quantity quantity,
                                               EventWriter &event_writer) noexcept;
    [[nodiscard]] ReplaceResult replace_order(OrderId id, const NewOrder &order,
                                              EventWriter &event_writer) noexcept;

    void end_session(EventWriter &event_writer) noexcept; // good for day purge

  private: // names from the core
    using Core::invalid_hash_slot;
    using Core::invalid_price_slot;
    using Core::invalid_resting_index;
    using typename Core::Level;
    using typename Core::RestingOrderNode;
    using typename Core::WalkControl;

  private: // matching
    template <Side AggressiveSide, bool StpActive>
    [[nodiscard]] MatchOutcome match_level(Level &level, Quantity &remaining, Price level_price,
                                           const NewOrder &order, EventWriter &event_writer);

    template <Side AggressiveSide, bool StpActive>
    [[nodiscard]] MatchOutcome match_order(Quantity &remaining, const NewOrder &order,
                                           EventWriter &event_writer);

    // validate and add
    [[nodiscard]] AddStatus validate_new_order(const NewOrder &order,
                                               bool check_duplicate) const noexcept;

    [[nodiscard]] AddResult add_validated_order(const NewOrder &order,
                                                EventWriter &event_writer) noexcept;

    // fill check
    template <bool StpActive>
    [[nodiscard]] MatchOutcome scan_level(const Level &level, Price level_price,
                                          const NewOrder &order,
                                          Quantity &remaining) const noexcept;

    template <Side S, bool StpActive>
    [[nodiscard]] bool can_fill_levels(const NewOrder &order) const noexcept;

    [[nodiscard]] bool can_fully_fill(const NewOrder &order) const noexcept;

  private: // data
    std::vector<OrderId> eod_purge_ids_;
};

} // namespace books
} // namespace lob

#include <lob/books/detail/dense_ladder_ob_impl.hpp>