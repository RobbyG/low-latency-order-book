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
}; // namespace tag

using OrderId = Scalar<std::uint64_t, tag::OrderId>;
using StpId = Scalar<std::uint32_t, tag::StpId>;

using Price = Scalar<std::int64_t, tag::Price>;
using Quantity = Scalar<std::uint64_t, tag::Quantity>;
using Notional = Scalar<__int128, tag::Notional>;

[[nodiscard]] constexpr Notional make_notional(Price price, Quantity quantity) noexcept {
    return Notional(static_cast<__int128>(price.get_value()) *
                    static_cast<__int128>(quantity.get_value()));
}

} // namespace lob