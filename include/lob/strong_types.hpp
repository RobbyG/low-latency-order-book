#pragma once

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

    [[nodiscard]] friend constexpr

        [[nodiscard]] constexpr T
        get_value() const noexcept {
        return value_;
    }

  private:
    T value_;
};

} // namespace lob