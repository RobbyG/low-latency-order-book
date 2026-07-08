#pragma once

#include <cstddef>
#include <functional>

namespace lob {
template <typename T, typename Tag> struct Scalar {
  public:
    constexpr Scalar() noexcept = default;
    explicit constexpr Scalar(T v) noexcept : value_(v) {}

    [[nodiscard]] friend constexpr bool operator==(Scalar, Scalar) noexcept = default;
    [[nodiscard]] friend constexpr auto operator<=>(Scalar, Scalar) noexcept = default;

    constexpr Scalar &operator-=(Scalar v) noexcept {
        value_ -= v.value_;
        return *this;
    }

    constexpr Scalar &operator+=(Scalar v) noexcept {
        value_ += v.value_;
        return *this;
    }

    [[nodiscard]] friend constexpr Scalar operator-(Scalar left, Scalar right) noexcept {
        left -= right;
        return left;
    }

    [[nodiscard]] friend constexpr Scalar operator+(Scalar left, Scalar right) noexcept {
        left += right;
        return left;
    }

    [[nodiscard]] constexpr T get_value() const noexcept {
        return value_;
    }

  private:
    T value_;
};

template <typename T, typename Tag> struct IdType {
  public:
    constexpr IdType() noexcept = default;
    explicit constexpr IdType(T v) noexcept : value_(v) {}

    [[nodiscard]] friend constexpr bool operator==(IdType, IdType) noexcept = default;
    [[nodiscard]] friend constexpr auto operator<=>(IdType, IdType) noexcept = default;

    [[nodiscard]] constexpr T get_value() const noexcept {
        return value_;
    }

  private:
    T value_;
};

} // namespace lob

template <typename T, typename Tag> struct std::hash<lob::IdType<T, Tag>> {
    [[nodiscard]] std::size_t operator()(lob::IdType<T, Tag> id) const noexcept {
        return std::hash<T>{}(id.get_value());
    }
};