# Low Latency Order Book

A C++ limit order book / matching engine project.

## Current status

Initial project infrastructure set-up

- C++23
- Cmake + Ninja build

- Core library
- Demo, Test and Benchmark executables

## Build

```bash
cmake -S . -B build =G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

