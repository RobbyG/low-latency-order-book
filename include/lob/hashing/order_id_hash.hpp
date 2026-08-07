#pragma once

#include <lob/domain_types.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lob::hashing {

template <class H>
concept OrderIdSlotHashPolicy =
    std::is_empty_v<H> && requires(OrderId id, std::size_t mask, unsigned shift) {
        { H::operator()(id) } noexcept -> std::same_as<std::uint64_t>;
        { H::hash_into_slot(id, mask, shift) } noexcept -> std::same_as<std::size_t>;
    };

struct IdentityHash {
    [[nodiscard]] static constexpr std::uint64_t operator()(OrderId id) noexcept {
        return id.get_value();
    }

    [[nodiscard]] static constexpr std::size_t hash_into_slot(OrderId id, std::size_t mask,
                                                              unsigned) noexcept {
        return (IdentityHash{}(id)) & mask;
    }
};

struct FibonacciHash {
    [[nodiscard]] static constexpr std::uint64_t operator()(OrderId id) noexcept {
        return static_cast<std::uint64_t>(id.get_value() * 0x9E3779B97F4A7C15ULL);
    }

    [[nodiscard]] static constexpr std::size_t hash_into_slot(OrderId id, std::size_t,
                                                              unsigned shift) noexcept {
        return (FibonacciHash{}(id)) >> shift;
    }
};

struct MultiplyShiftXorX1 {
    [[nodiscard]] static constexpr std::uint64_t operator()(OrderId id) noexcept {
        std::uint64_t x = id.get_value();
        x ^= x >> 33;
        x *= 0xFF51AFD7ED558CCDULL;
        return x;
    }

    [[nodiscard]] static constexpr std::size_t hash_into_slot(OrderId id, std::size_t mask,
                                                              unsigned) noexcept {
        return (MultiplyShiftXorX1{}(id)) & mask;
    }
};

struct SplitMix64 {
    [[nodiscard]] static constexpr std::uint64_t operator()(OrderId id) noexcept {
        std::uint64_t x = id.get_value();
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ULL;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBULL;
        x ^= x >> 31;
        return x;
    }

    [[nodiscard]] static constexpr std::size_t hash_into_slot(OrderId id, std::size_t mask,
                                                              unsigned) noexcept {
        return (SplitMix64{}(id)) & mask;
    }
};

struct XXH3Avalanche {
    [[nodiscard]] static constexpr std::uint64_t operator()(OrderId id) noexcept {
        std::uint64_t x = id.get_value();
        x ^= x >> 37;
        x *= 0x165667919E3779F9ULL;
        x ^= x >> 32;
        return x;
    }

    [[nodiscard]] static constexpr std::size_t hash_into_slot(OrderId id, std::size_t mask,
                                                              unsigned) noexcept {
        return (XXH3Avalanche{}(id)) & mask;
    }
};

} // namespace lob::hashing