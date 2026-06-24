#pragma once

#include <lob/order_book.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace lob {

class Engine {
  public:
    Engine() : trade_ring_(std::make_unique<TradeRing>()), event_writer_(*trade_ring_) {
        order_book_.reserve(1u << 20, 1u << 16);
    }

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

    OrderBook order_book_;
    EventWriter event_writer_;

    std::unique_ptr<TradeRing> trade_ring_;
    std::unique_ptr<CommandRing> command_ring_;

    std::atomic<bool> stop_engine_requested_{false};
    std::atomic<bool> input_thread_done_{false};
    std::atomic<bool> book_thread_done_{false};
    std::atomic<bool> output_thread_done_{false};
};

} // namespace lob