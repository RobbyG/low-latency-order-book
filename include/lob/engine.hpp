#pragma once

#include <lob/books/order_book_concept.hpp>
#include <lob/command.hpp>
#include <lob/trade_writer.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace lob {

using CommandRing = SpscRing<Command, command_ring_capacity>;

template <books::OrderBookCore Book> class Engine final {
  public:
    template <typename... Args>
        requires std::constructible_from<Book, Args...>
    explicit Engine(Args &&...args)
        : command_ring_(std::make_unique<CommandRing>()),
          trade_ring_(std::make_unique<TradeRing>()), trade_writer_(*trade_ring_),
          order_book_(std::forward<Args>(args)...) {}

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    void start();
    void stop();

    ~Engine() {
        stop();
    }

  private:
    void input_loop();
    void book_loop();
    void output_loop();

    std::thread input_thread_;
    std::thread book_thread_;
    std::thread output_thread_;

    std::unique_ptr<CommandRing> command_ring_;
    std::unique_ptr<TradeRing> trade_ring_;

    TradeWriter trade_writer_;
    Book order_book_;

    std::atomic<bool> stop_engine_requested_{false};
    std::atomic<bool> input_thread_done_{false};
    std::atomic<bool> book_thread_done_{false};
    std::atomic<bool> output_thread_done_{false};
};

} // namespace lob