#include <chrono>
#include <cstdint>
#include <iostream>

#include <lob/order_book.hpp>

using i64 = std::int64_t;

int main() {

    constexpr int iterations = 10'000'000;

    i64 result = 0;

    const auto start = std::chrono::steady_clock::now();

    const auto end = std::chrono::steady_clock::now();

    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Performed " << iterations << " calls to add_numbers.\n";
    std::cout << "Result: " << result << '\n';
    std::cout << "Duration: " << duration << " nanoseconds\n";
    std::cout << "Average time per call: " << static_cast<double>(duration) / iterations
              << " nanoseconds\n";

    return 0;
}