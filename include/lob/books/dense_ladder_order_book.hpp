#pragma once

#include <lob/domain_types.hpp>
#include <lob/hashing/order_id_hash.hpp>
#include <lob/order.hpp>
#include <lob/order_book_results.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace lob {

class TradeWriter;

namespace books {

template <std::size_t BandWidth, lob::hashing::OrderIdSlotHashPolicy Hash>
class DenseLadderOrderBook final {

  public:
    struct Config {
        Price base{};
        std::uint32_t reserve_orders{};
    };
    explicit DenseLadderOrderBook(Config config);

    DenseLadderOrderBook(const DenseLadderOrderBook &) = delete;
    DenseLadderOrderBook &operator=(const DenseLadderOrderBook &) = delete;

    DenseLadderOrderBook(DenseLadderOrderBook &&) = delete;
    DenseLadderOrderBook &operator=(DenseLadderOrderBook &&) = delete;

    [[nodiscard]] AddResult add_order(const NewOrder &order, TradeWriter &trade_writer);
    [[nodiscard]] CancelResult cancel_order(OrderId id) noexcept;
    [[nodiscard]] ReduceResult reduce_order_by(OrderId id, Quantity quantity) noexcept;
    [[nodiscard]] ReplaceResult replace_order(OrderId id, const NewOrder &order,
                                              TradeWriter &trade_writer);

    [[nodiscard]] bool best_bid(PriceQuantity &pq) const noexcept;
    [[nodiscard]] bool best_ask(PriceQuantity &pq) const noexcept;
    [[nodiscard]] bool best_price_quantity(Side side, PriceQuantity &pq) const noexcept;

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
    static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

    static_assert(BandWidth > 0 && BandWidth <= invalid_index,
                  "BandWidth must be positive and within limits");

    struct Level {
        Quantity total_quantity{};
        std::uint32_t head{invalid_index};
        std::uint32_t tail{invalid_index};
    };
    static_assert(sizeof(Level) == 16, "Level must be 16 bytes in size");

    struct RestingOrderNode {
        OrderId id;
        Quantity quantity;
        StpId stp_id;
        TimeInForce time_in_force;

        std::uint32_t prev{invalid_index};
        std::uint32_t next{invalid_index};
    };
    static_assert(sizeof(RestingOrderNode) == 32, "RestingOrderNode must be 32 bytes in size");

    struct IdEntry {
        OrderId id{};
        Price price{};
        std::uint32_t node_index{invalid_index};
        Side side;
    };
    static_assert(sizeof(IdEntry) == 24, "IdEntry must be 24 bytes in size");

    using Levels = std::array<Level, BandWidth>;
    using Occupancy = std::array<std::uint64_t, (BandWidth + 63) / 64>;
    using OverflowLevels = std::vector<std::pair<Price, Level>>;

    Config config_;

    Levels bids_{};
    Levels asks_{};

    Occupancy bids_occupied_{};
    Occupancy asks_occupied_{};

    std::size_t best_bid_slot_{invalid_index};
    std::size_t best_ask_slot_{invalid_index};

    OverflowLevels bids_overflow_;
    OverflowLevels asks_overflow_;

    std::vector<RestingOrderNode> order_pool_;
    std::uint32_t resting_pool_head_{invalid_index};

    std::vector<IdEntry> order_index_;
    std::size_t order_index_mask_{};
    std::size_t order_index_shift_{};

    void reserve(const Config &config);

    [[nodiscard]] AddStatus validate_new_order(const NewOrder &order) const noexcept;

    [[nodiscard]] AddResult add_validated_order(const NewOrder &order, TradeWriter &trade_writer);

    template <typename OppositeLevels, typename OppositeOccupancy, typename OppositeOverflowLevels,
              typename SameSideLevels, typename SameSideOccupancy, typename SameSideOverflowLevels>
    [[nodiscard]] AddResult
    match_and_add(OppositeLevels &opposite_levels, OppositeOccupancy &opposite_occupancy,
                  OppositeOverflowLevels &opposite_overflow_levels,
                  SameSideLevels &same_side_levels, SameSideOccupancy &same_side_occupancy,
                  SameSideOverflowLevels &same_side_overflow_levels, NewOrder &order,
                  TradeWriter &trade_writer);

    [[nodiscard]] const std::size_t find_id_entry(OrderId id) const noexcept;

    [[nodiscard]] std::size_t previous_occupied_slot(const auto &occupied,
                                                     std::size_t slot) noexcept;
    [[nodiscard]] std::size_t next_occupied_slot(const auto &occupied, std::size_t slot) noexcept;

    template <Side OppositeSide, bool ExcludeOrder>
    [[nodiscard]] bool can_fill_levels(const NewOrder &order,
                                       OrderId excluded_id = {}) const noexcept;

    [[nodiscard]] bool can_fully_fill(const NewOrder &order) const noexcept;

    [[nodiscard]] bool can_fully_fill(const NewOrder &order, OrderId id) const noexcept;
};

} // namespace books
} // namespace lob