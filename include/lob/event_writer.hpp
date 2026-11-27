#pragma once

#include <lob/market_event.hpp>
#include <lob/rings_config.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/thread_util.hpp>

namespace lob {

using EventRing = SpscRing<MarketEvent, event_ring_capacity>;

// matcher stalls if event ring is full, ring size should be large enough to avoid this, but if it
// happens, we pause the CPU

class EventWriter final {
  public:
    explicit EventWriter(EventRing &ring) noexcept : event_ring_(ring) {}

    void set_command_timestamp(Timestamp t) noexcept {
        timestamp_ = t;
    }

    void write_event(const EventType &event_type) noexcept {
        const MarketEvent event{.seq = seq_number_++, .t = timestamp_, .type = event_type};
        while (!event_ring_.push(event)) {
            pause_cpu();
        }
    }

  private:
    EventRing &event_ring_;
    SequenceNumber seq_number_{};
    Timestamp timestamp_{};
};
} // namespace lob