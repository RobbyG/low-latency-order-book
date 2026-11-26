#pragma once

#include <lob/domain_types.hpp>
#include <lob/hardware_constants.hpp>
#include <lob/order.hpp>

#include <type_traits>
#include <variant>

namespace lob {

struct AddCommand {
    NewOrder order;
};

struct CancelCommand {
    OrderId id;
};

struct ReduceCommand {
    OrderId id;
    Quantity quantity;
};

struct ReplaceCommand {
    OrderId id;
    NewOrder order;
};

struct EndSessionCommand {};

using CommandType =
    std::variant<AddCommand, CancelCommand, ReduceCommand, ReplaceCommand, EndSessionCommand>;

struct alignas(cache_line_size) Command {
    Timestamp t;
    CommandType type;
};

static_assert(std::is_trivially_copyable_v<Command>, "all commands must be trivially copyable");
static_assert(sizeof(Command) == cache_line_size);

} // namespace lob