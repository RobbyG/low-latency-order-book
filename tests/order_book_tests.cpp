#include <cassert>

#include <lob/order_book.hpp>

int main() {
    assert(lob::add_numbers(2, 3) == 5);
    assert(lob::add_numbers(-1, 1) == 0);
    assert(lob::add_numbers(0, 0) == 0);
    assert(lob::add_numbers(-5, -10) == -15);

    return 0;
}