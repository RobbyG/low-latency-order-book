// Included by <lob/engine/engine.hpp>. Do not include directly.

#pragma once

#include <lob/books/ob_concepts.hpp>
#include <lob/command.hpp>
#include <lob/engine/engine.hpp>
#include <lob/event_writer.hpp>
#include <lob/input/source_concepts.hpp>
#include <lob/instrument.hpp>
#include <lob/spsc_ring.hpp>

namespace lob::engine {

// construction

template <books::OrderBookSkeleton Book, books::OrderBookMirror Mirror, input::InputSource Input>
Engine<Book, Mirror, Input>::Engine(typename Book::Config book_config,
                                    typename Mirror::Config mirror_config, Input input)
    : command_ring_(), event_ring_(), input_(std::move(input)), event_writer_(event_ring_),
      book_(book_config), mirror_(mirror_config) {
    if (mirror_config.max_orders < book_config.max_orders)
        throw std::invalid_argument(
            "mirror must be able to hold at least as many orders as the book");
}

// engine manipulation API

template <books::OrderBookSkeleton Book, books::OrderBookMirror Mirror, input::InputSource Input>
void Engine<Book, Mirror, Input>::start() {
    input_thread_ = std::jthread([this](std::stop_token st) { input_loop(st); });
    book_thread_ = std::jthread([this](std::stop_token st) { book_loop(st); });
    mirror_thread_ = std::jthread([this](std::stop_token st) { mirror_loop(st); });
}

template <books::OrderBookSkeleton Book, books::OrderBookMirror Mirror, input::InputSource Input>
void Engine<Book, Mirror, Input>::stop() noexcept {
    input_thread_.request_stop();
    if (input_thread_.joinable())
        input_thread_.join();
    book_thread_.request_stop();
    if (book_thread_.joinable())
        book_thread_.join();
    mirror_thread_.request_stop();
    if (mirror_thread_.joinable())
        mirror_thread_.join();
}

template <books::OrderBookSkeleton Book, books::OrderBookMirror Mirror, input::InputSource Input>
void Engine<Book, Mirror, Input>::wait() noexcept {
    if (input_thread_.joinable())
        input_thread_.join();
    if (book_thread_.joinable())
        book_thread_.join();
    if (mirror_thread_.joinable())
        mirror_thread_.join();
}

// thread loops

template <books::OrderBookSkeleton Book, books::OrderBookMirror Mirror, input::InputSource Input>
void Engine<Book, Mirror, Input>::input_loop(std::stop_token st) noexcept {
    while (!st.stop_requested() && !input_.finished()) {

        Command command;
        if (!input_.poll(command)) {
            pause_cpu();
            continue;
        }

        while (!command_ring_.push(command))
            pause_cpu();
    }
    input_done_.store(true, std::memory_order_release);
}

} // namespace lob::engine