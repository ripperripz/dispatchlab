#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "dispatchlab/binary_heap.hpp"
#include "dispatchlab/counters.hpp"
#include "dispatchlab/graph.hpp"
#include "dispatchlab/routing.hpp"
#include "dispatchlab/scenario.hpp"
#include "dispatchlab/types.hpp"

namespace dispatchlab {

class DispatchPolicy;

// ---------------------------------------------------------------- events

// The numeric value is the tie-break priority for events with equal
// timestamps (lower first): completions free couriers before new work is
// dispatched, and assignments decided at time t are applied after every other
// event at t.
enum class EventType : std::uint8_t {
    OrderDelivered = 0,
    CourierAvailable = 1,
    FoodReady = 2,
    CourierArrivedRestaurant = 3,
    OrderPickedUp = 4,
    OrderCreated = 5,
    CourierAssigned = 6,
};
inline constexpr int kEventTypeCount = 7;

std::string_view event_name(EventType t);
EventType parse_event_name(std::string_view s);

inline constexpr std::uint32_t kNone = UINT32_MAX;

struct Event {
    SimTime time = 0;
    EventType type = EventType::OrderCreated;
    std::uint64_t id = 0;  // assigned by the queue in scheduling order
    std::uint32_t order = kNone;
    std::uint32_t courier = kNone;
};

// Priority queue ordered by (timestamp, event priority, event id), built on the
// project's BinaryHeap. Event ids increase monotonically, so equal
// (time, priority) events are processed in scheduling order.
class EventQueue {
public:
    std::uint64_t push(SimTime time, EventType type, std::uint32_t order, std::uint32_t courier);
    Event pop();
    const Event& top() const { return heap_.top(); }
    bool empty() const { return heap_.empty(); }
    std::size_t size() const { return heap_.size(); }
    std::uint64_t scheduled() const { return next_id_; }

    struct Less {
        bool operator()(const Event& a, const Event& b) const {
            if (a.time != b.time) return a.time < b.time;
            if (a.type != b.type) return static_cast<int>(a.type) < static_cast<int>(b.type);
            return a.id < b.id;
        }
    };

private:
    BinaryHeap<Event, Less> heap_;
    std::uint64_t next_id_ = 0;
};

// One line of the event log. `from/to/travel_ms/distance_mm/path` are only
// meaningful for COURIER_ASSIGNED (courier -> restaurant leg) and
// ORDER_PICKED_UP (restaurant -> customer leg).
struct LogEvent {
    std::uint64_t seq = 0;
    SimTime timestamp = 0;
    EventType type = EventType::OrderCreated;
    std::uint32_t order = kNone;
    std::uint32_t courier = kNone;
    NodeId location = kInvalidNode;
    NodeId from = kInvalidNode;
    NodeId to = kInvalidNode;
    SimTime travel_ms = 0;
    std::uint64_t distance_mm = 0;
    std::vector<NodeId> path;
};

std::string log_event_to_json(const LogEvent& e, bool with_path);
LogEvent log_event_from_json(std::string_view line);
std::vector<LogEvent> read_event_log(const std::string& path);

// ---------------------------------------------------------------- state

// Completing = delivered, COURIER_AVAILABLE not yet processed.
enum class CourierStatus : std::uint8_t { Idle, ToRestaurant, AtRestaurant, ToCustomer, Completing };
enum class OrderStatus : std::uint8_t { NotCreated, Pending, Assigned, PickedUp, Delivered };

struct CourierState {
    NodeId location = kInvalidNode;
    CourierStatus status = CourierStatus::Idle;
    std::uint32_t order = kNone;
    SimTime busy_since = 0;
    SimTime busy_ms = 0;  // completed busy intervals
    SimTime leg_end = 0;  // scheduled arrival time of the current travel leg
    std::uint64_t distance_mm = 0;
    std::uint32_t deliveries = 0;
};

struct OrderState {
    OrderStatus status = OrderStatus::NotCreated;
    std::uint32_t courier = kNone;
    bool food_ready = false;
    bool courier_at_restaurant = false;
    SimTime created_at = -1, ready_at = -1, assigned_at = -1, arrived_at = -1, picked_at = -1,
            delivered_at = -1;
};

struct SimState {
    SimTime now = 0;
    std::uint64_t events_applied = 0;
    std::uint32_t created = 0, assigned = 0, picked = 0, delivered = 0;
    std::vector<CourierState> couriers;
    std::vector<OrderState> orders;
};

// Stable FNV-1a 64 hash over a canonical little-endian serialization of the
// state (field order fixed, see state_hash in simulator.cpp).
std::uint64_t state_hash(const SimState& s);

struct InvariantViolation : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Applies logged events to the state, enforcing the PRD §13.3 invariants and
// state-machine legality. Used by the simulator itself (every transition goes
// through here) and by replay, which therefore reconstructs state from the log
// alone. Throws InvariantViolation on any illegal transition.
class StateMachine {
public:
    // paranoid: run the O(orders) global check after every event (check-log, tests).
    explicit StateMachine(const Scenario& sc, bool paranoid = false);
    void apply(const LogEvent& e);
    const SimState& state() const { return state_; }
    // Full re-check of global invariants (also run after each apply()).
    void check_global() const;

private:
    [[noreturn]] void fail(const LogEvent& e, const std::string& what) const;
    const Scenario& sc_;
    bool paranoid_;
    SimState state_;
};

// ---------------------------------------------------------------- simulator

struct SimOptions {
    bool log_paths = false;  // include route node lists in the log (viewer)
    std::function<void(const LogEvent&)> on_event;  // optional sink
};

struct SimMetrics {
    std::uint32_t orders_total = 0;
    std::uint32_t orders_created = 0;
    std::uint32_t orders_delivered = 0;
    std::uint32_t orders_undelivered = 0;
    double avg_delivery_min = 0;  // creation -> delivery, delivered orders only
    double p50_delivery_min = 0;
    double p95_delivery_min = 0;
    double max_delivery_min = 0;
    double avg_wait_for_courier_min = 0;  // creation -> assignment
    double courier_utilization = 0;       // busy time / (couriers * end_time)
    double total_distance_km = 0;
    std::uint64_t dispatch_decisions = 0;
    double assign_latency_mean_us = 0;  // wall clock; excluded from logs/hashes
    double assign_latency_p95_us = 0;
    double assign_latency_max_us = 0;
    std::uint64_t routing_queries = 0;  // simulator legs + policy searches
    std::uint64_t events = 0;
    SimTime last_event_time = 0;
};

struct SimResult {
    SimMetrics metrics;
    std::uint64_t final_state_hash = 0;
    std::vector<LogEvent> log;
    SimState final_state;
};

SimMetrics compute_metrics(const Scenario& sc, const SimState& st);

// Deterministic discrete-event simulation of one scenario under one policy.
// `g` must already carry the scenario's traffic weights.
class Simulator {
public:
    Simulator(const Graph& g, const Scenario& sc, DispatchPolicy& policy, Algorithm routing,
              SimOptions opts = {});
    SimResult run();

private:
    void emit(LogEvent e);
    void try_dispatch(SimTime now);
    void handle(const Event& ev);

    const Graph& g_;
    const Scenario& sc_;
    DispatchPolicy& policy_;
    Router router_;
    SimOptions opts_;
    StateMachine sm_;
    EventQueue queue_;
    std::deque<std::uint32_t> pending_;           // orders awaiting a courier, FIFO
    std::vector<char> courier_reserved_;          // decided but ASSIGNED not yet applied
    std::vector<LogEvent> log_;
    std::vector<double> latencies_us_;
    std::uint64_t routing_queries_ = 0;
};

// ---------------------------------------------------------------- replay

struct ReplayResult {
    SimState state;
    std::uint64_t final_state_hash = 0;
    std::uint64_t events = 0;
};

// Reconstructs the final state from the scenario and the event log only. Takes
// no graph, router or policy, so no routing or dispatch can happen.
ReplayResult replay(const Scenario& sc, const std::vector<LogEvent>& log);

// Deliberately corrupted copies of a valid log, one per PRD §13.3 invariant
// class (delivery before pickup, double delivery, time moving backward,
// courier double-assignment, pickup before food ready). Used to prove that
// the invariant checker rejects them. Kinds that cannot be constructed from
// the given log are omitted.
std::vector<std::pair<std::string, std::vector<LogEvent>>> make_corrupted_logs(const std::vector<LogEvent>& log);

}  // namespace dispatchlab
