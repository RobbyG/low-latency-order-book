#pragma once

#include <lob/strong_types.hpp>

#include <cstdint>

namespace lob {

namespace tag {
struct OrderId {};
struct StpId {};

struct Price {};
struct Quantity {};
struct Notional {};
} // namespace tag

using OrderId = IdType<std::uint64_t, tag::OrderId>;
using StpId = IdType<std::uint32_t, tag::StpId>;

using Price = Scalar<std::int64_t, tag::Price>;
using Quantity = Scalar<std::uint64_t, tag::Quantity>;

#if defined(__GNUC__) || defined(__clang__)
using Int128 = __int128;
#else
#error "lob requires compiler support for signed 128-bit integers"
#endif

using Notional = Scalar<Int128, tag::Notional>;

enum class Side : std::uint8_t { Buy, Sell };

[[nodiscard]] constexpr Notional make_notional(Price price, Quantity quantity) noexcept {
    return Notional(static_cast<Int128>(price.get_value()) *
                    static_cast<Int128>(quantity.get_value()));
}

} // namespace lob