#include "dispatchlab/simulator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/hash.hpp"

namespace dispatchlab {

// ---------------------------------------------------------------- state hash

std::uint64_t state_hash(const SimState& s) {
    Fnv1a64 h;
    h.add_str("DLSTATE1");
    h.add_i64(s.now);
    h.add_u64(s.events_applied);
    h.add_u32(s.created);
    h.add_u32(s.assigned);
    h.add_u32(s.picked);
    h.add_u32(s.delivered);
    h.add_u64(s.couriers.size());
    for (const auto& c : s.couriers) {
        h.add_u32(c.location);
        h.add_u8(static_cast<std::uint8_t>(c.status));
        h.add_u32(c.order);
        h.add_i64(c.busy_since);
        h.add_i64(c.busy_ms);
        h.add_i64(c.leg_end);
        h.add_u64(c.distance_mm);
        h.add_u32(c.deliveries);
    }
    h.add_u64(s.orders.size());
    for (const auto& o : s.orders) {
        h.add_u8(static_cast<std::uint8_t>(o.status));
        h.add_u32(o.courier);
        h.add_u8(o.food_ready ? 1 : 0);
        h.add_u8(o.courier_at_restaurant ? 1 : 0);
        h.add_i64(o.created_at);
        h.add_i64(o.ready_at);
        h.add_i64(o.assigned_at);
        h.add_i64(o.arrived_at);
        h.add_i64(o.picked_at);
        h.add_i64(o.delivered_at);
    }
    return h.digest();
}

// ---------------------------------------------------------------- state machine

StateMachine::StateMachine(const Scenario& sc, bool paranoid) : sc_(sc), paranoid_(paranoid) {
    state_.couriers.resize(sc.couriers.size());
    for (std::size_t i = 0; i < sc.couriers.size(); ++i) state_.couriers[i].location = sc.couriers[i].location;
    state_.orders.resize(sc.orders.size());
}

void StateMachine::fail(const LogEvent& e, const std::string& what) const {
    std::ostringstream os;
    os << "invariant violated at event seq=" << e.seq << " t=" << e.timestamp << " " << event_name(e.type)
       << " order=" << static_cast<std::int64_t>(e.order == kNone ? -1 : e.order)
       << " courier=" << static_cast<std::int64_t>(e.courier == kNone ? -1 : e.courier) << ": " << what;
    throw InvariantViolation(os.str());
}

void StateMachine::apply(const LogEvent& e) {
    SimState& s = state_;
    // Event stream integrity and monotone time.
    if (e.seq != s.events_applied) fail(e, "sequence number gap");
    if (e.timestamp < s.now) fail(e, "simulation time moved backward");
    if (e.timestamp > sc_.end_time) fail(e, "event after simulation end");

    auto need_order = [&]() -> OrderState& {
        if (e.order == kNone || e.order >= s.orders.size()) fail(e, "invalid order id");
        return s.orders[e.order];
    };
    auto need_courier = [&]() -> CourierState& {
        if (e.courier == kNone || e.courier >= s.couriers.size()) fail(e, "invalid courier id");
        return s.couriers[e.courier];
    };
    auto check_leg = [&](NodeId from, NodeId to) {
        if (e.from != from || e.to != to) fail(e, "travel leg endpoints inconsistent");
        if (e.travel_ms < 0) fail(e, "negative travel time");
        if (!e.path.empty() && (e.path.front() != from || e.path.back() != to)) fail(e, "path endpoints inconsistent");
    };

    switch (e.type) {
        case EventType::OrderCreated: {
            OrderState& o = need_order();
            const OrderSpec& spec = sc_.orders[e.order];
            if (e.courier != kNone) fail(e, "creation carries a courier");
            if (o.status != OrderStatus::NotCreated) fail(e, "order created twice");
            if (e.timestamp != spec.created_at) fail(e, "creation time differs from scenario");
            if (e.location != sc_.restaurants[spec.restaurant_id].location) fail(e, "creation location != restaurant");
            o.status = OrderStatus::Pending;
            o.created_at = e.timestamp;
            ++s.created;
            break;
        }
        case EventType::FoodReady: {
            OrderState& o = need_order();
            const OrderSpec& spec = sc_.orders[e.order];
            if (o.status == OrderStatus::NotCreated) fail(e, "food ready before order creation");
            if (o.food_ready) fail(e, "food ready twice");
            if (e.timestamp != o.created_at + spec.preparation_ms) fail(e, "ready time != creation + preparation");
            if (e.location != sc_.restaurants[spec.restaurant_id].location) fail(e, "ready location != restaurant");
            o.food_ready = true;
            o.ready_at = e.timestamp;
            break;
        }
        case EventType::CourierAssigned: {
            OrderState& o = need_order();
            CourierState& c = need_courier();
            const OrderSpec& spec = sc_.orders[e.order];
            // Each order has at most one active courier assignment.
            if (o.status != OrderStatus::Pending || o.courier != kNone) fail(e, "order not awaiting assignment");
            // A courier cannot hold multiple simultaneous deliveries.
            if (c.status != CourierStatus::Idle || c.order != kNone) fail(e, "courier already busy");
            if (e.location != c.location) fail(e, "assignment location != courier location");
            check_leg(c.location, sc_.restaurants[spec.restaurant_id].location);
            o.status = OrderStatus::Assigned;
            o.courier = e.courier;
            o.assigned_at = e.timestamp;
            c.status = CourierStatus::ToRestaurant;
            c.order = e.order;
            c.busy_since = e.timestamp;
            c.leg_end = e.timestamp + e.travel_ms;
            c.distance_mm += e.distance_mm;
            ++s.assigned;
            break;
        }
        case EventType::CourierArrivedRestaurant: {
            OrderState& o = need_order();
            CourierState& c = need_courier();
            const OrderSpec& spec = sc_.orders[e.order];
            if (o.status != OrderStatus::Assigned || o.courier != e.courier) fail(e, "arrival for unassigned order");
            if (c.status != CourierStatus::ToRestaurant || c.order != e.order) fail(e, "courier not travelling to restaurant");
            if (e.timestamp != c.leg_end) fail(e, "arrival time != departure + travel time");
            if (e.location != sc_.restaurants[spec.restaurant_id].location) fail(e, "arrival location != restaurant");
            c.status = CourierStatus::AtRestaurant;
            c.location = e.location;
            o.courier_at_restaurant = true;
            o.arrived_at = e.timestamp;
            break;
        }
        case EventType::OrderPickedUp: {
            OrderState& o = need_order();
            CourierState& c = need_courier();
            const OrderSpec& spec = sc_.orders[e.order];
            if (o.status != OrderStatus::Assigned || o.courier != e.courier) fail(e, "pickup of order not assigned to courier");
            // Pickup only after the courier reached the restaurant and the food is ready.
            if (!o.courier_at_restaurant || c.status != CourierStatus::AtRestaurant || c.order != e.order) {
                fail(e, "pickup before courier reached restaurant");
            }
            if (!o.food_ready || e.timestamp < o.ready_at) fail(e, "pickup before food ready");
            if (e.timestamp < o.arrived_at) fail(e, "pickup before arrival");
            const NodeId r = sc_.restaurants[spec.restaurant_id].location;
            if (e.location != r) fail(e, "pickup location != restaurant");
            check_leg(r, sc_.customers[spec.customer_id].location);
            o.status = OrderStatus::PickedUp;
            o.picked_at = e.timestamp;
            c.status = CourierStatus::ToCustomer;
            c.leg_end = e.timestamp + e.travel_ms;
            c.distance_mm += e.distance_mm;
            ++s.picked;
            break;
        }
        case EventType::OrderDelivered: {
            OrderState& o = need_order();
            CourierState& c = need_courier();
            const OrderSpec& spec = sc_.orders[e.order];
            if (o.status == OrderStatus::Delivered) fail(e, "order delivered twice");
            // Delivery cannot occur before pickup.
            if (o.status != OrderStatus::PickedUp || o.courier != e.courier) fail(e, "delivery before pickup");
            if (c.status != CourierStatus::ToCustomer || c.order != e.order) fail(e, "courier not delivering this order");
            if (e.timestamp != c.leg_end || e.timestamp < o.picked_at) fail(e, "delivery time inconsistent with travel");
            if (e.location != sc_.customers[spec.customer_id].location) fail(e, "delivery location != customer");
            o.status = OrderStatus::Delivered;
            o.delivered_at = e.timestamp;
            c.status = CourierStatus::Completing;
            c.location = e.location;
            ++s.delivered;
            break;
        }
        case EventType::CourierAvailable: {
            CourierState& c = need_courier();
            if (e.order != kNone) fail(e, "availability carries an order");
            if (c.status != CourierStatus::Completing) fail(e, "courier available without completing a delivery");
            if (e.location != c.location) fail(e, "availability location != courier location");
            c.busy_ms += e.timestamp - c.busy_since;
            c.status = CourierStatus::Idle;
            c.order = kNone;
            ++c.deliveries;
            break;
        }
    }
    s.now = e.timestamp;
    ++s.events_applied;
    // Cheap counters invariants every event; full scan in paranoid mode.
    if (s.delivered > s.created || s.delivered > s.picked || s.picked > s.assigned || s.assigned > s.created) {
        fail(e, "counter invariant (delivered <= picked <= assigned <= created) broken");
    }
    if (paranoid_) check_global();
}

void StateMachine::check_global() const {
    const SimState& s = state_;
    std::uint32_t created = 0, delivered = 0;
    std::vector<std::uint32_t> active(s.couriers.size(), 0);
    for (std::size_t i = 0; i < s.orders.size(); ++i) {
        const OrderState& o = s.orders[i];
        if (o.status != OrderStatus::NotCreated) ++created;
        if (o.status == OrderStatus::Delivered) {
            ++delivered;
            if (!(o.created_at <= o.assigned_at && o.assigned_at <= o.arrived_at && o.arrived_at <= o.picked_at &&
                  o.ready_at <= o.picked_at && o.picked_at <= o.delivered_at)) {
                throw InvariantViolation("order " + std::to_string(i) + " has non-monotone timestamps");
            }
        }
        if (o.status == OrderStatus::Assigned || o.status == OrderStatus::PickedUp) {
            if (o.courier >= s.couriers.size()) throw InvariantViolation("active order without courier");
            if (++active[o.courier] > 1) throw InvariantViolation("courier holds two active orders");
            if (s.couriers[o.courier].order != i) throw InvariantViolation("courier/order cross-reference broken");
        }
    }
    if (created != s.created || delivered != s.delivered) throw InvariantViolation("counters disagree with orders");
    if (delivered > created) throw InvariantViolation("DeliveredOrders > CreatedOrders");
}

// ---------------------------------------------------------------- metrics

namespace {

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    // Nearest-rank percentile.
    const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
    return v[std::min(v.size() - 1, rank == 0 ? 0 : rank - 1)];
}

}  // namespace

SimMetrics compute_metrics(const Scenario& sc, const SimState& st) {
    SimMetrics m;
    m.orders_total = static_cast<std::uint32_t>(sc.orders.size());
    m.orders_created = st.created;
    m.orders_delivered = st.delivered;
    m.orders_undelivered = m.orders_total - st.delivered;
    std::vector<double> dur;
    double wait_sum = 0;
    std::uint32_t waits = 0;
    for (const auto& o : st.orders) {
        if (o.status == OrderStatus::Delivered) dur.push_back(static_cast<double>(o.delivered_at - o.created_at) / 60000.0);
        if (o.assigned_at >= 0) {
            wait_sum += static_cast<double>(o.assigned_at - o.created_at) / 60000.0;
            ++waits;
        }
    }
    if (!dur.empty()) {
        double sum = 0;
        for (double d : dur) sum += d;
        m.avg_delivery_min = sum / static_cast<double>(dur.size());
        m.p50_delivery_min = percentile(dur, 50);
        m.p95_delivery_min = percentile(dur, 95);
        m.max_delivery_min = *std::max_element(dur.begin(), dur.end());
    }
    m.avg_wait_for_courier_min = waits ? wait_sum / waits : 0.0;
    double busy = 0;
    std::uint64_t dist = 0;
    for (const auto& c : st.couriers) {
        busy += static_cast<double>(c.busy_ms);
        if (c.status != CourierStatus::Idle) busy += static_cast<double>(sc.end_time - c.busy_since);
        dist += c.distance_mm;
    }
    const double denom = static_cast<double>(sc.couriers.size()) * static_cast<double>(sc.end_time);
    m.courier_utilization = denom > 0 ? busy / denom : 0.0;
    m.total_distance_km = static_cast<double>(dist) / 1e6;
    m.events = st.events_applied;
    m.last_event_time = st.now;
    return m;
}

// ---------------------------------------------------------------- simulator

Simulator::Simulator(const Graph& g, const Scenario& sc, DispatchPolicy& policy, Algorithm routing, SimOptions opts)
    : g_(g),
      sc_(sc),
      policy_(policy),
      router_(g, routing),
      opts_(std::move(opts)),
      sm_(sc),
      courier_reserved_(sc.couriers.size(), 0) {
    if (sc.graph_nodes != g.num_nodes()) throw std::invalid_argument("scenario was generated for a different graph");
}

void Simulator::emit(LogEvent e) {
    e.seq = log_.size();
    sm_.apply(e);  // every transition is validated against the invariants
    if (opts_.on_event) opts_.on_event(e);
    log_.push_back(std::move(e));
}

void Simulator::try_dispatch(SimTime now) {
    std::vector<CourierCandidate> available;
    while (!pending_.empty()) {
        available.clear();
        const auto& cs = sm_.state().couriers;
        for (std::uint32_t c = 0; c < cs.size(); ++c) {
            if (cs[c].status == CourierStatus::Idle && !courier_reserved_[c]) available.push_back({c, cs[c].location});
        }
        if (available.empty()) return;  // order stays queued until a courier frees up
        const std::uint32_t o = pending_.front();
        const OrderSpec& spec = sc_.orders[o];
        DispatchRequest req{o, sc_.restaurants[spec.restaurant_id].location, sc_.customers[spec.customer_id].location,
                            now, spec.created_at + spec.preparation_ms};
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint32_t c = policy_.choose(req, available);
        latencies_us_.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        const bool valid = std::any_of(available.begin(), available.end(), [&](const auto& a) { return a.id == c; });
        if (!valid) throw std::logic_error("dispatch policy chose an unavailable courier");
        pending_.pop_front();
        courier_reserved_[c] = 1;
        queue_.push(now, EventType::CourierAssigned, o, c);
    }
}

void Simulator::handle(const Event& ev) {
    const SimState& st = sm_.state();
    LogEvent e;
    e.timestamp = ev.time;
    e.type = ev.type;
    e.order = ev.order;
    e.courier = ev.courier;
    switch (ev.type) {
        case EventType::OrderCreated: {
            const OrderSpec& spec = sc_.orders[ev.order];
            e.location = sc_.restaurants[spec.restaurant_id].location;
            emit(std::move(e));
            queue_.push(ev.time + spec.preparation_ms, EventType::FoodReady, ev.order, kNone);
            pending_.push_back(ev.order);
            try_dispatch(ev.time);
            break;
        }
        case EventType::FoodReady: {
            const OrderSpec& spec = sc_.orders[ev.order];
            e.location = sc_.restaurants[spec.restaurant_id].location;
            emit(std::move(e));
            const OrderState& o = st.orders[ev.order];
            if (o.courier_at_restaurant && o.status == OrderStatus::Assigned) {
                queue_.push(ev.time, EventType::OrderPickedUp, ev.order, o.courier);
            }
            break;
        }
        case EventType::CourierAssigned: {
            const OrderSpec& spec = sc_.orders[ev.order];
            const NodeId from = st.couriers[ev.courier].location;
            const NodeId to = sc_.restaurants[spec.restaurant_id].location;
            RouteResult r = router_.route(from, to, true);
            ++routing_queries_;
            if (!r.reachable()) throw std::runtime_error("restaurant unreachable for courier");
            e.location = from;
            e.from = from;
            e.to = to;
            e.travel_ms = static_cast<SimTime>(r.cost);
            e.distance_mm = path_length_mm(g_, r.path);
            if (opts_.log_paths) e.path = std::move(r.path);
            courier_reserved_[ev.courier] = 0;
            emit(std::move(e));
            queue_.push(ev.time + static_cast<SimTime>(r.cost), EventType::CourierArrivedRestaurant, ev.order, ev.courier);
            break;
        }
        case EventType::CourierArrivedRestaurant: {
            const OrderSpec& spec = sc_.orders[ev.order];
            e.location = sc_.restaurants[spec.restaurant_id].location;
            emit(std::move(e));
            if (st.orders[ev.order].food_ready) queue_.push(ev.time, EventType::OrderPickedUp, ev.order, ev.courier);
            break;
        }
        case EventType::OrderPickedUp: {
            const OrderSpec& spec = sc_.orders[ev.order];
            const NodeId from = sc_.restaurants[spec.restaurant_id].location;
            const NodeId to = sc_.customers[spec.customer_id].location;
            RouteResult r = router_.route(from, to, true);
            ++routing_queries_;
            if (!r.reachable()) throw std::runtime_error("customer unreachable from restaurant");
            e.location = from;
            e.from = from;
            e.to = to;
            e.travel_ms = static_cast<SimTime>(r.cost);
            e.distance_mm = path_length_mm(g_, r.path);
            if (opts_.log_paths) e.path = std::move(r.path);
            emit(std::move(e));
            queue_.push(ev.time + static_cast<SimTime>(r.cost), EventType::OrderDelivered, ev.order, ev.courier);
            break;
        }
        case EventType::OrderDelivered: {
            const OrderSpec& spec = sc_.orders[ev.order];
            e.location = sc_.customers[spec.customer_id].location;
            emit(std::move(e));
            queue_.push(ev.time, EventType::CourierAvailable, kNone, ev.courier);
            break;
        }
        case EventType::CourierAvailable: {
            e.location = st.couriers[ev.courier].location;
            emit(std::move(e));
            try_dispatch(ev.time);
            break;
        }
    }
}

SimResult Simulator::run() {
    for (const auto& o : sc_.orders) queue_.push(o.created_at, EventType::OrderCreated, o.id, kNone);
    while (!queue_.empty() && queue_.top().time <= sc_.end_time) handle(queue_.pop());
    sm_.check_global();

    SimResult res;
    res.final_state = sm_.state();
    res.final_state_hash = state_hash(res.final_state);
    res.metrics = compute_metrics(sc_, res.final_state);
    res.metrics.dispatch_decisions = latencies_us_.size();
    if (!latencies_us_.empty()) {
        double sum = 0;
        for (double x : latencies_us_) sum += x;
        res.metrics.assign_latency_mean_us = sum / static_cast<double>(latencies_us_.size());
        res.metrics.assign_latency_p95_us = percentile(latencies_us_, 95);
        res.metrics.assign_latency_max_us = *std::max_element(latencies_us_.begin(), latencies_us_.end());
    }
    res.metrics.routing_queries = routing_queries_ + policy_.routing_queries();
    res.log = std::move(log_);
    return res;
}

}  // namespace dispatchlab
