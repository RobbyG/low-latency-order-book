#pragma once

#include <cstddef>
#include <new>

namespace lob {

#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t cache_line_size = 64;
#endif
} // namespace lob