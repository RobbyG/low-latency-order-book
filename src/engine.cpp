#include <lob/engine.hpp>

namespace lob {

namespace {
void log_trade(const Trade &trade) noexcept {
    (void)trade;
}

void write_exec_report(const Trade &trade) noexcept {
    (void)trade;
}
} // namespace

void Engine::start() {
    book_thread_ = std::thread([this] { book_loop(); });
    output_thread_ = std::thread([this] { output_loop(); });
}

void Engine::stop() {
    stop_engine_requested_.store(true, std::memory_order_relaxed);

    if (book_thread_.joinable()) {
        book_thread_.join();
    }

    if (output_thread_.joinable()) {
        output_thread_.join();
    }
}

void Engine::book_loop() {
    pin_to_core(2);
    Command cmd;
    while (!stop_engine_requested_.load(std::memory_order_relaxed)) {
        if (command_ring_.pop(cmd)) {
            switch (cmd.type) {
            case CommandType::Add:
                (void)order_book_.add_order(cmd.add, trade_writer_);
                break;
            case CommandType::Cancel:
                (void)order_book_.cancel_order(cmd.cancel.id);
                break;
            case CommandType::Reduce:
                (void)order_book_.reduce_order_by(cmd.reduce.id, cmd.reduce.new_quantity);
                break;
            case CommandType::Replace:
                (void)order_book_.replace_order(cmd.replace.id, cmd.replace.new_order,
                                                trade_writer_);
                break;
            }
        } else {
            pause_cpu();
        }
    }
    book_thread_done_.store(true, std::memory_order_release);
}

void Engine::output_loop() {
    pin_to_core(4);
    Trade trade;
    while (true) {
        if (trade_ring_->pop(trade)) {
            log_trade(trade);
            write_exec_report(trade);
            continue;
        }
        if (book_thread_done_.load(std::memory_order_acquire)) {
            while (trade_ring_->pop(trade)) { // drain the tail
                log_trade(trade);
                write_exec_report(trade);
            }
            break;
        }
        pause_cpu();
    }
}

} // namespace lob