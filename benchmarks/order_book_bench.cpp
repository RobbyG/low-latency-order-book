#include <chrono>
#include <cstdint>
#include <iostream>

#include <lob/order_book.hpp>

using i64 = std::int64_t;

int main() {

    constexpr int iterations = 10'000;

    i64 result = 0;

    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; ++i) {
        lob::OrderBook order_book;

        for (uint64_t j = 0; j < 100; ++j) {
            auto trades = order_book.add_order({j, 100 + j, 10, lob::Side::Buy});
            result += trades.size();
        }

        for (uint64_t j = 0; j < 100; ++j) {
            auto trades = order_book.add_order({100 + j, 100 + j, 10, lob::Side::Sell});
            result += trades.size();
        }
    }

    const auto end = std::chrono::steady_clock::now();

    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Performed " << iterations << " calls to add_order.\n";
    std::cout << "Result: " << result << '\n';
    std::cout << "Duration: " << duration << " nanoseconds\n";
    std::cout << "Average time per call: " << static_cast<double>(duration) / iterations
              << " nanoseconds\n";

    return 0;
}