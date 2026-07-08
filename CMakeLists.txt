cmake_minimum_required(VERSION 3.25)

project(low_latency_order_book
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#main library and executable 

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

#testing setup

enable_testing()

add_executable(lob_test
    tests/order_book_tests.cpp
)

target_link_libraries(lob_test
    PRIVATE
        lob_core
)

add_test(NAME lob_test COMMAND lob_test)

add_executable(map_list_order_book_test
    tests/map_list_order_book_test.cpp)

target_link_libraries(map_list_order_book_test PRIVATE lob_core)

add_test(NAME map_list_order_book_test COMMAND map_list_order_book_test)


#benchmarking setup

add_executable(lob_bench
    benchmarks/order_book_bench.cpp
)

target_link_libraries(lob_bench
    PRIVATE
        lob_core
)

