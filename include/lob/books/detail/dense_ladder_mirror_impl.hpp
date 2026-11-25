// Included by <lob/books/dense_ladder_mirror.hpp>. Do not include directly.

#pragma once

#include <lob/books/dense_ladder_mirror.hpp>
#include <lob/market_event.hpp>
#include <lob/overloaded.hpp>
#include <lob/trade.hpp>

#include <cassert>
#include <cstddef>

namespace lob::books {

// API
template <std::size_t BandWidth, lob::hashing::OrderIdHashPolicy Hash>
void DenseLadderMirror<BandWidth, Hash>::apply_event(const MarketEvent &event) noexcept {

    std::visit(
        Overloaded{
            [&](const OrderRested &e) {
                const NewOrder order{.id = e.id,
                                     .price = e.price,
                                     .quantity = e.quantity,
                                     .stp_id = e.stp_id,
                                     .side = e.side,
                                     .order_type = OrderType::Limit,
                                     .time_in_force = e.time_in_force};
                [[maybe_unused]] const AddResult result =
                    e.side == Side::Buy ? this->template rest_order<Side::Buy>(e.quantity, order)
                                        : this->template rest_order<Side::Sell>(e.quantity, order);
                assert(result.status == AddStatus::Rested &&
                       "mirror must succeed at resting an order");
            },
            [&](const OrderCancelled &e) {
                [[maybe_unused]] const CancelResult result = this->remove_order(e.id);
                assert(result.status == CancelStatus::Cancelled &&
                       "mirror must succeed at cancelling an existing order");
            },
            [&](const OrderReduced &e) {
                OrderView view{};
                [[maybe_unused]] const bool found = this->find_order(e.id, view);
                assert(found && "mirror must hold every reduced order");
                [[maybe_unused]] const ReduceResult result =
                    this->reduce_order(e.id, view.quantity - e.new_quantity);
                assert(result.status == ReduceStatus::Reduced && "mirror reduce must succeed");
            },
            [](const OrderRejected &) {},
            [&](const Trade &e) {
                [[maybe_unused]] const ReduceResult result =
                    this->reduce_order(e.resting_order_id, e.quantity);
                assert((result.status == ReduceStatus::Reduced ||
                        result.status == ReduceStatus::Cancelled) &&
                       "mirror must either reduce the quantity or cancel the order if the "
                       "reduction would result in a remaining quantity of 0");
            },
        },
        event.type);
}

} // namespace lob::books