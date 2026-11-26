#pragma once

#include <lob/command.hpp>

namespace lob::input {

template <typename Source>
concept InputSource = requires(Source &source, const Source &const_source, Command &command) {
    { source.poll(command) } -> std::same_as<bool>;
    { const_source.finished() } -> std::same_as<bool>;
};

} // namespace lob::input