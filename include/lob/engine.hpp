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

template <books::OrderBookSkeleton Book> class Engine final {
  public:
    explicit Engine(typename Book::Config config)
        : command_ring_{}, event_ring_{}, event_writer_(event_ring_), order_book_(config) {}

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    void start();
    void stop() noexcept;

    ~Engine() {
        stop();
    }

  private:
    void input_loop();
    void book_loop();
    void output_loop();

    CommandRing command_ring_;
    EventRing event_ring_;
    EventWriter event_writer_;

    alignas(cache_line_size) Book order_book_;

    std::jthread input_thread_;
    std::jthread book_thread_;
    std::jthread output_thread_;

    std::atomic<bool> stop_engine_requested_{false};
    std::atomic<bool> input_thread_done_{false};
    std::atomic<bool> book_thread_done_{false};
    std::atomic<bool> output_thread_done_{false};
};

} // namespace lob