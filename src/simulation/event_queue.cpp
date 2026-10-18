#include <stdexcept>
#include <string>

#include "dispatchlab/simulator.hpp"

namespace dispatchlab {

namespace {
constexpr std::string_view kEventNames[kEventTypeCount] = {
    "ORDER_DELIVERED", "COURIER_AVAILABLE", "FOOD_READY", "COURIER_ARRIVED_RESTAURANT",
    "ORDER_PICKED_UP", "ORDER_CREATED",     "COURIER_ASSIGNED",
};
}  // namespace

std::string_view event_name(EventType t) { return kEventNames[static_cast<int>(t)]; }

EventType parse_event_name(std::string_view s) {
    for (int i = 0; i < kEventTypeCount; ++i) {
        if (kEventNames[i] == s) return static_cast<EventType>(i);
    }
    throw std::invalid_argument("unknown event type: " + std::string(s));
}

std::uint64_t EventQueue::push(SimTime time, EventType type, std::uint32_t order, std::uint32_t courier) {
    Event e;
    e.time = time;
    e.type = type;
    e.id = next_id_++;
    e.order = order;
    e.courier = courier;
    heap_.push(e);
    return e.id;
}

Event EventQueue::pop() { return heap_.take(); }

}  // namespace dispatchlab
