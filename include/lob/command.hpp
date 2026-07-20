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
    NewOrder order;
};
static_assert(std::is_trivially_copyable_v<ReplaceCmd>);

struct Command {
    CommandType type;

    union {
        std::byte dummy;
        AddCmd add;
        CancelCmd cancel;
        ReduceCmd reduce;
        ReplaceCmd replace;
    };

    Command() noexcept : type(CommandType::None), dummy{} {}

    static Command make_add(NewOrder order) noexcept {
        Command cmd;
        cmd.type = CommandType::Add;
        cmd.add = AddCmd{order};
        return cmd;
    }

    static Command make_cancel(OrderId id) noexcept {
        Command cmd;
        cmd.type = CommandType::Cancel;
        cmd.cancel = CancelCmd{id};
        return cmd;
    }

    static Command make_reduce(OrderId id, Quantity new_quantity) noexcept {
        Command cmd;
        cmd.type = CommandType::Reduce;
        cmd.reduce = ReduceCmd{id, new_quantity};
        return cmd;
    }

    static Command make_replace(OrderId id, NewOrder order) noexcept {
        Command cmd;
        cmd.type = CommandType::Replace;
        cmd.replace = ReplaceCmd{id, order};
        return cmd;
    }
};
static_assert(sizeof(Command) == 48, "Command must be 48 bytes in size");
static_assert(std::is_trivially_copyable_v<Command>);

} // namespace lob