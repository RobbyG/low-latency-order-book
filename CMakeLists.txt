cmake_minimum_required(VERSION 3.25)

project(low_latency_order_book
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(lob_core
    src/order_book.cpp
)

add_executable(lob_demo
    src/main.cpp
)

target_link_libraries(lob_demo
    PRIVATE
        lob_core
)

target_include_directories(lob_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(lob_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
)