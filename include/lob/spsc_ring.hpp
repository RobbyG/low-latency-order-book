#pragma once

#include <array>
#include <atomic>
#include <new>
#include <type_traits>

namespace lob {

#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t cache_line = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t cache_line = 64;
#endif

// Single Producer Single Consumer Ring Buffer

template <typename T, std::size_t Capacity> class alignas(cache_line) SpscRing {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscRing only supports trivially copyable types");
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "SpscRing capacity must be a power of two and at least 2");

  public:
    SpscRing() = default;

    SpscRing(const SpscRing &) = delete;
    SpscRing &operator=(const SpscRing &) = delete;
    SpscRing(SpscRing &&) = delete;
    SpscRing &operator=(SpscRing &&) = delete;

    [[nodiscard]] bool push(const T &value) noexcept {
        const auto current_head = head_.load(std::memory_order_relaxed);
        const std::size_t next_head = (current_head + 1) & mask;

        if (next_head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (next_head == cached_tail_) {
                return false;
            }
        }

        buffer_[current_head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(T &value) noexcept {
        const auto current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return false;
            }
        }

        value = buffer_[current_tail];
        tail_.store((current_tail + 1) & mask, std::memory_order_release);
        return true;
    }

  private:
    static constexpr std::size_t mask = Capacity - 1;

    alignas(cache_line) std::atomic<std::size_t> head_{0};
    std::size_t cached_tail_{0};

    alignas(cache_line) std::atomic<std::size_t> tail_{0};
    std::size_t cached_head_{0};

    alignas(cache_line) std::array<T, Capacity> buffer_;
};

} // namespace lob