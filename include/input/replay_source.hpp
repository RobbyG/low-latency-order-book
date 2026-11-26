#pragma once

#include <lob/command.hpp>

namespace lob {
namespace input {

class ReplaySource {

  public: // constants and types
    enum class ReplayMode { MaximumThroughput, Timestamped };

  public: // construction
    ReplaySource(std::vector<Command> commands, ReplayMode mode)
        : commands_(std::move(commands)), mode_(mode) {}

    // copy and move disabled

    ReplaySource(const ReplaySource &) = delete;
    ReplaySource &operator=(const ReplaySource &) = delete;

    ReplaySource(ReplaySource &&) = default;
    ReplaySource &operator=(ReplaySource &&) = default;

    // control API
    bool poll(Command &command) noexcept;
    bool finished() const noexcept;

  private: // data members
    std::vector<Command> commands_;
    std::size_t next_{0};
    ReplayMode mode_;
};

} // namespace input
} // namespace lob
