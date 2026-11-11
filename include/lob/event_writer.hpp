#pragma once

#include <lob/market_event.hpp>
#include <lob/rings_config.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/thread_util.hpp>
#include <lob/trade.hpp>

namespace lob {

using EventRing = SpscRing<Trade, trade_ring_capacity>;

// matcher stalls if event ring is full, ring size should be large enough to avoid this, but if it
// happens, we pause the CPU to avoid busy waiting

class EventWriter final {
  public:
    explicit EventWriter(EventRing &trade_ring) noexcept : trade_ring_(trade_ring) {}

    void on_trade(const Trade &trade) noexcept {
        while (!trade_ring_.push(trade)) {
            pause_cpu();
        }
    }

  private:
    EventRing &trade_ring_;
};
} // namespace lob