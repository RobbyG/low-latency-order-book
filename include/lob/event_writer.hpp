#pragma once

#include <lob/market_event.hpp>
#include <lob/rings_config.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/thread_util.hpp>

namespace lob {

using EventRing = SpscRing<MarketEvent, event_ring_capacity>;

// matcher stalls if event ring is full, ring size should be large enough to avoid this, but if it
// happens, we pause the CPU to avoid busy waiting

class EventWriter final {
  public:
    explicit EventWriter(EventRing &event_ring) noexcept : event_ring_(event_ring) {}

    void on_event(const MarketEvent &event) noexcept {
        while (!event_ring_.push(event)) {
            pause_cpu();
        }
    }

  private:
    EventRing &event_ring_;
};
} // namespace lob