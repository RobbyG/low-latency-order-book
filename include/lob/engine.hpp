#pragma once

#include <lob/command.hpp>
#include <lob/order_book.hpp>
#include <lob/trade_writer.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace lob {

using CommandRing = SpscRing<Command, command_ring_capacity>;

class Engine {
  public:
    Engine()
        : command_ring_(std::make_unique<CommandRing>()),
          trade_ring_(std::make_unique<TradeRing>()), trade_writer_(*trade_ring_),
          order_book_(OrderBookConfig{
              .max_orders = 1u << 20,
              .max_price_levels = 1u << 16,
          }) {}

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
    OrderBook order_book_;

    std::atomic<bool> stop_engine_requested_{false};
    std::atomic<bool> input_thread_done_{false};
    std::atomic<bool> book_thread_done_{false};
    std::atomic<bool> output_thread_done_{false};
};

} // namespace lob