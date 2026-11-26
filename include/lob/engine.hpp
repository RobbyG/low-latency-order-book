#pragma once

#include <lob/books/ob_concepts.hpp>
#include <lob/command.hpp>
#include <lob/event_writer.hpp>
#include <lob/instrument.hpp>
#include <lob/rings_config.hpp>
#include <lob/spsc_ring.hpp>

#include <atomic>
#include <concepts>
#include <memory>
#include <thread>
#include <utility>

namespace lob {

using CommandRing = SpscRing<Command, command_ring_capacity>;

template <books::OrderBookSkeleton Book, books::OrderBookMirror Mirror, input::InputSource Input>
class Engine final {

  public: // construction
    explicit Engine(typename Book::Config config) : {}

    // copy and move disabled
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    // engine manipulation API
    void start();
    void stop() noexcept;

    ~Engine() {
        stop();
    }

  private:
};

} // namespace lob