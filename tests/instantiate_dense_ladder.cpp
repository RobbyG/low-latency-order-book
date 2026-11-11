// tests/instantiate_dense_ladder.cpp
#include <lob/books/dense_ladder_ob.hpp>

template class lob::books::DenseLadderOrderBook<1024, lob::hashing::FibonacciHash>;