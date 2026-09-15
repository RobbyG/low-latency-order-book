cmake_minimum_required(VERSION 3.25)

project(low_latency_order_book
    VERSION 0.3.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# Common headers
# ============================================================

add_library(lob_headers INTERFACE)

target_include_directories(lob_headers
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

# ============================================================
# Main compiled code
# ============================================================

add_library(lob_core STATIC
    src/engine.cpp
    src/books/map_list_order_book.cpp
)

target_link_libraries(lob_core
    PUBLIC
        lob_headers
)

target_compile_options(lob_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
)

# ============================================================
# Dense ladder compile check
# ============================================================

add_library(dense_compile_check OBJECT
    tests/instantiate_dense_ladder.cpp
)

target_link_libraries(dense_compile_check
    PRIVATE
        lob_headers
)

target_compile_options(dense_compile_check
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
)