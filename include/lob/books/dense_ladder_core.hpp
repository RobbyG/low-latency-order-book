#pragma once

#include <lob/domain_types.hpp>
#include <lob/hashing/order_id_hash.hpp>
#include <lob/order.hpp>
#include <lob/order_book_results.hpp>

#include <array>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace lob {

namespace books {

template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash> class DenseLadderCore {

  public: // types
    struct Config {
        Price base{};
        std::uint32_t max_orders{};
    };

  protected: // construction
    explicit DenseLadderCore(Config config);
    ~DenseLadderCore() = default;

  public: // copy and move disabled
    DenseLadderCore(const DenseLadderCore &) = delete;
    DenseLadderCore &operator=(const DenseLadderCore &) = delete;

    DenseLadderCore(DenseLadderCore &&) = delete;
    DenseLadderCore &operator=(DenseLadderCore &&) = delete;

  public: // views
    // L1 view API
    [[nodiscard]] bool best_bid(PriceQuantity &pq) const noexcept;
    [[nodiscard]] bool best_ask(PriceQuantity &pq) const noexcept;
    [[nodiscard]] bool best_price_quantity(Side side, PriceQuantity &pq) const noexcept;

    // L2 view API
    [[nodiscard]] CopyResult copy_bid_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_ask_depth(std::span<PriceLevel> out) const noexcept;
    [[nodiscard]] CopyResult copy_depth(Side side, std::span<PriceLevel> out) const noexcept;

    // L3 view API
    [[nodiscard]] bool find_order(OrderId id, OrderView &out) const noexcept;
    [[nodiscard]] CopyResult copy_best_bid_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_best_ask_orders(std::span<OrderView> out) const noexcept;
    [[nodiscard]] CopyResult copy_orders_at_price(Side side, Price price,
                                                  std::span<OrderView> out) const noexcept;

  public: // extra public API
    void reset() noexcept;

  protected: // constants and types
    static constexpr std::uint32_t invalid_resting_index =
        std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t invalid_hash_slot = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t invalid_price_slot = std::numeric_limits<std::size_t>::max();
    static_assert(BandWidth > 0 && BandWidth <= invalid_resting_index,
                  "BandWidth must be positive and within limits");

    struct Level {
        Quantity total_quantity{};
        std::uint32_t head{invalid_resting_index};
        std::uint32_t tail{invalid_resting_index};
    };
    static_assert(sizeof(Level) == 16, "Level must be 16 bytes in size");

    struct RestingOrderNode {
        OrderId id;
        Quantity quantity;
        StpId stp_id;
        TimeInForce time_in_force;

        std::uint32_t prev{invalid_resting_index};
        std::uint32_t next{invalid_resting_index};
    };
    static_assert(sizeof(RestingOrderNode) == 32, "RestingOrderNode must be 32 bytes in size");

    struct OrderLocation {
        OrderId id{};
        Price price{};
        std::uint32_t resting_index{invalid_resting_index};
        Side side;
    };
    static_assert(sizeof(OrderLocation) == 24, "OrderLocation must be 24 bytes in size");

    enum class WalkControl : std::uint8_t { Continue, Stop };

    using Ladder = std::array<Level, BandWidth>;
    using LadderOccupancy = std::array<std::uint64_t, (BandWidth + 63) / 64>;
    using OverflowLevels = std::vector<std::pair<Price, Level>>;

  protected: // derived class API
    template <Side S> void walk_side(this auto &self, Price limit, auto &&visit);

    template <Side S> [[nodiscard]] AddResult rest_order(Quantity remaining, const NewOrder &order);

    CancelResult remove_order(OrderId id) noexcept;

    void remove_order_from_location(std::size_t slot) noexcept;

    void remove_resting_from_level(Level &level, RestingOrderNode &node,
                                   std::uint32_t resting_index) noexcept;

    bool remove_slot_from_order_locations(OrderId id) noexcept;

    ReduceResult reduce_order(OrderId id, Quantity quantity) noexcept;

    [[nodiscard]] std::size_t find_order_location(OrderId id) const noexcept;

  private: // internal helpers
    // construction
    void reserve(const Config &config);

    // L1 view
    template <Side S> [[nodiscard]] bool best_level(PriceQuantity &pq) const noexcept;

    // L2 view
    template <Side S>
    [[nodiscard]] CopyResult copy_levels(std::span<PriceLevel> out) const noexcept;
    template <Side S> [[nodiscard]] std::uint32_t level_count() const noexcept;

    // L3 view
    template <Side S>
    [[nodiscard]] CopyResult copy_orders_at_price(Price price,
                                                  std::span<OrderView> out) const noexcept;

    template <Side S> [[nodiscard]] const Level *best_price_level(Price &price) const noexcept;

    template <Side S>
    [[nodiscard]] CopyResult copy_level(const Level &level, Price price,
                                        std::span<OrderView> out) const noexcept;

    template <Side S> [[nodiscard]] const Level *find_level(Price price) const noexcept;

    template <Side S> [[nodiscard]] Level *find_level(Price price) noexcept {
        return const_cast<Level *>(std::as_const(*this).template find_level<S>(price));
    }

    // walk
    template <Side S, typename LevelsType>
    [[nodiscard]] static WalkControl walk_overflow(LevelsType &levels, Price limit, auto &&visit);

    template <Side S, typename LevelsType>
    [[nodiscard]] static WalkControl walk_ladder(LevelsType &levels, auto &occupied,
                                                 auto &best_slot, Price base, Price limit,
                                                 auto &&visit);

    // order management
    // cancel/remove

    template <Side S>
    void remove_resting_at_index(Price price, std::uint32_t resting_index) noexcept;

    void remove_slot_from_order_locations(std::size_t slot) noexcept;

    // rest
    template <Side S>
    void rest_ladder(Ladder &levels, LadderOccupancy &occupied, std::size_t &best_slot, Price price,
                     std::uint32_t resting_index, Quantity quantity) noexcept;

    template <Side S>
    void rest_overflow(OverflowLevels &levels, Price price, std::uint32_t resting_index,
                       Quantity quantity);

    void append_to_level(Level &level, std::uint32_t resting_index, Quantity quantity) noexcept;

  private: // building blocks
    template <Side S> decltype(auto) with_side(this auto &self, auto &&f) {
        if constexpr (S == Side::Buy)
            return f(self.bids_better_overflow_, self.bids_, self.bids_occupied_,
                     self.bids_best_slot_, self.bids_worse_overflow_);
        else
            return f(self.asks_better_overflow_, self.asks_, self.asks_occupied_,
                     self.asks_best_slot_, self.asks_worse_overflow_);
    }

    template <Side S> bool better_than_band(Price price) const noexcept {
        if constexpr (S == Side::Buy) {
            if (price > ladder_high_)
                return true;
            else
                return false;
        } else {
            if (price < ladder_low_)
                return true;
            else
                return false;
        }
    }

    bool in_band(Price price) const noexcept {
        if (price >= ladder_low_ && price <= ladder_high_)
            return true;
        return false;
    }

    [[nodiscard]] static constexpr std::size_t price_diff_to_size_t(Price price,
                                                                    Price base) noexcept {
        assert(price >= base);
        return static_cast<std::size_t>((price - base).get_value());
    }

    template <Side S, typename LevelsType>
    [[nodiscard]] static auto overflow_find_level(LevelsType &levels, Price price) noexcept {
        return std::lower_bound(
            levels.begin(), levels.end(), price,
            [](const auto &entry, Price value) { return worse<S>(value, entry.first); });
    }

    [[nodiscard]] std::size_t probe_slot(OrderId) const noexcept;

    // ladder occupancy
    static decltype(auto) is_occupied(auto &occupied, std::size_t slot) noexcept {
        return (occupied[slot >> 6] & std::uint64_t{1} << (slot & 63)) != 0;
    }

    static void set_occupied(LadderOccupancy &occupied, std::size_t slot) noexcept {
        occupied[slot >> 6] |= std::uint64_t{1} << (slot & 63);
    }
    static void clear_occupied(LadderOccupancy &occupied, std::size_t slot) noexcept {
        occupied[slot >> 6] &= ~(std::uint64_t{1} << (slot & 63));
    }

    template <Side S>
    [[nodiscard]] static std::size_t next_worse_ladder_slot(const LadderOccupancy &occupied,
                                                            std::size_t slot) noexcept;

    [[nodiscard]] static std::size_t lower_occupied_slot(const LadderOccupancy &occupied,
                                                         std::size_t slot) noexcept;
    [[nodiscard]] static std::size_t higher_occupied_slot(const LadderOccupancy &occupied,
                                                          std::size_t slot) noexcept;

  protected: // data
    Config config_;

    Price ladder_low_;
    Price ladder_high_;

    Ladder bids_{};
    Ladder asks_{};

    // one bit per level; set means the level has resting orders
    // slot s is bit (s % 64) of word (s / 64), so bit 0 of word 0 is the lowest price in the
    // band
    LadderOccupancy bids_occupied_{};
    LadderOccupancy asks_occupied_{};

    // price_slot = price - ladder_low_; invalid_price_slot when the level is empty
    std::size_t bids_best_slot_{invalid_price_slot};
    std::size_t asks_best_slot_{invalid_price_slot};

    OverflowLevels bids_better_overflow_, bids_worse_overflow_;
    OverflowLevels asks_better_overflow_, asks_worse_overflow_;

    std::vector<RestingOrderNode> resting_order_pool_;
    std::uint32_t resting_order_pool_head_{};

    std::vector<OrderLocation> order_location_table_;
    std::size_t hash_mask_{};
    unsigned hash_shift_{};
};

} // namespace books
} // namespace lob

#include <lob/books/detail/dense_ladder_core_impl.hpp>