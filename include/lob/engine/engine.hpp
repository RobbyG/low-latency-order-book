#pragma once

#include <lob/books/ob_concepts.hpp>
#include <lob/command.hpp>
#include <lob/event_writer.hpp>
#include <lob/input/source_concepts.hpp>
#include <lob/instrument.hpp>
#include <lob/rings_config.hpp>
#include <lob/spsc_ring.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lob::engine {

using CommandRing = SpscRing<Command, command_ring_capacity>;

template <books::OrderBookSkeleton Book, books::OrderBookMirror Mirror, input::InputSource Input>
class Engine final {

  public: // construction
    explicit Engine(typename Book::Config book_config, typename Mirror::Config mirror_config,
                    Input input);

    // copy and move disabled
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    // engine manipulation API
    void start();
    void stop() noexcept;
    void wait() noexcept;

    ~Engine() {
        stop();
    }

  private: // thread loops
    void input_loop(std::stop_token st) noexcept;
    void book_loop(std::stop_token st) noexcept;
    void mirror_loop(std::stop_token st) noexcept;

  private: // spsc rings
    CommandRing command_ring_;
    EventRing event_ring_;

    // input thread data
    alignas(cache_line_size) Input input_;

    // book thread data
    alignas(cache_line_size) EventWriter event_writer_;
    Book book_;

    // mirror thread data
    alignas(cache_line_size) Mirror mirror_;

    // thread stoppers
    alignas(cache_line_size) std::atomic<bool> input_done_{false};
    std::atomic<bool> book_done_{false};

    // threads
    std::jthread input_thread_;
    std::jthread book_thread_;
    std::jthread mirror_thread_;
};

} // namespace lob::engine

#include <lob/engine/detail/engine_impl.hpp>