#pragma once

#include <lob/rings_config.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/thread_util.hpp>
#include <lob/trade.hpp>

namespace lob {

using TradeRing = SpscRing<Trade, trade_ring_capacity>;

class TradeWriter final {
  public:
    explicit TradeWriter(TradeRing &trade_ring) noexcept : trade_ring_(trade_ring) {}

    void on_trade(const Trade &trade) noexcept {
        while (!trade_ring_.push(trade)) {
            pause_cpu();
        }
    }

  private:
    TradeRing &trade_ring_;
};
} // namespace lob