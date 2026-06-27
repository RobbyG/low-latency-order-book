#pragma once

#include <lob/domain_types.hpp>
#include <lob/order.hpp>

#include <cstddef>

namespace lob {
enum class CommandType : std::uint8_t { None, Add, Cancel, Reduce, Replace };

struct AddCmd {
    NewOrder order;
};
static_assert(std::is_trivially_copyable_v<AddCmd>);

struct CancelCmd {
    OrderId id;
};
static_assert(std::is_trivially_copyable_v<CancelCmd>);

struct ReduceCmd {
    OrderId id;
    Quantity new_quantity;
};
static_assert(std::is_trivially_copyable_v<ReduceCmd>);

struct ReplaceCmd {
    OrderId id;
    NewOrder new_order;
};
static_assert(std::is_trivially_copyable_v<ReplaceCmd>);

struct Command {
    CommandType type;

    union {
        std::byte dummy;
        NewOrder add;
        CancelCmd cancel;
        ReduceCmd reduce;
        ReplaceCmd replace;
    };

    Command() noexcept : type(CommandType::None), dummy{} {}

    static Command make_add(NewOrder order) noexcept {
        Command cmd;
        cmd.type = CommandType::Add;
        cmd.add = order;
        return cmd;
    }

    static Command make_cancel(CancelCmd cancel) noexcept {
        Command cmd;
        cmd.type = CommandType::Cancel;
        cmd.cancel = cancel;
        return cmd;
    }

    static Command make_reduce(ReduceCmd reduce) noexcept {
        Command cmd;
        cmd.type = CommandType::Reduce;
        cmd.reduce = reduce;
        return cmd;
    }

    static Command make_replace(ReplaceCmd replace) noexcept {
        Command cmd;
        cmd.type = CommandType::Replace;
        cmd.replace = replace;
        return cmd;
    }
};
static_assert(std::is_trivially_copyable_v<Command>);

} // namespace lob