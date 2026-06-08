v0.1.0 design:
- does not contain a fully working order_book yet, only the skeleton

v0.2.0 design:
- contains fully working order book - naive implementation

- std::map<Price, std::list<Order>> for bids/asks
- unordered_map<OrderId, OrderLocation> for tracking
- std::vector<Trade> returned from add_order
- best quantity calculated by iterating over best price level when asked

Some performance limitations:
- std::map allocation and pointer chasing 
- std::list allocation and poor cache locality 
- unordered_map may allocate/rehash
- vector<Trade> may allocate
- best_bid_quantity / best_ask_quantity are O(n) - worst case

- testing done with simple in-program asserts

v0.3.0 design:
