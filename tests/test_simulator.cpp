#include <set>

#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/simulator.hpp"
#include "dispatchlab/traffic.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

namespace {

// One courier at node 0, restaurant at node 2, customers at nodes 4 / 3 / 1 on
// a line graph with 1-minute edges.
Scenario line_scenario(const Graph& g, std::vector<OrderSpec> orders, std::vector<NodeId> customer_nodes,
                       std::vector<NodeId> courier_nodes = {0}) {
    Scenario s;
    s.graph_source = g.meta.source;
    s.graph_nodes = g.num_nodes();
    s.restaurants = {{0, 2}};
    for (std::uint32_t i = 0; i < customer_nodes.size(); ++i) s.customers.push_back({i, customer_nodes[i]});
    for (std::uint32_t i = 0; i < courier_nodes.size(); ++i) s.couriers.push_back({i, courier_nodes[i]});
    s.orders = std::move(orders);
    s.params.couriers = static_cast<std::uint32_t>(courier_nodes.size());
    s.params.orders = static_cast<std::uint32_t>(s.orders.size());
    s.end_time = 10 * 3600000;
    return s;
}

SimResult run_policy(const Graph& g, const Scenario& sc, const char* policy, Algorithm algo = Algorithm::AStar) {
    auto p = make_policy(policy, g);
    Simulator sim(g, sc, *p, algo);
    return sim.run();
}

}  // namespace

TEST_CASE("event queue: ordered by (timestamp, priority, id)") {
    EventQueue q;
    q.push(100, EventType::OrderCreated, 1, kNone);
    q.push(50, EventType::CourierAssigned, 2, 0);
    q.push(100, EventType::OrderDelivered, 3, 1);
    q.push(100, EventType::OrderCreated, 4, kNone);
    q.push(50, EventType::FoodReady, 5, kNone);
    std::vector<std::uint32_t> order;
    SimTime last = -1;
    while (!q.empty()) {
        Event e = q.pop();
        CHECK(e.time >= last);
        last = e.time;
        order.push_back(e.order);
    }
    // t=50: FoodReady(prio 2) before CourierAssigned(6); t=100: Delivered(0), then
    // the two OrderCreated in scheduling (id) order.
    CHECK(order == std::vector<std::uint32_t>{5, 2, 3, 1, 4});
}

TEST_CASE("event queue: stable for many simultaneous events") {
    EventQueue q;
    for (std::uint32_t i = 0; i < 1000; ++i) q.push(7, EventType::OrderCreated, i, kNone);
    for (std::uint32_t i = 0; i < 1000; ++i) CHECK(q.pop().order == i);
}

TEST_CASE("simulator: single delivery produces the exact PRD event sequence") {
    Graph g = fixtures::line(5);
    // Order 0 at t=0, prep 5 min. Courier 0->2 takes 2 min, food ready at 5,
    // pickup at 5, 2 min to node 4, delivered at 7 min.
    Scenario sc = line_scenario(g, {{0, 0, 0, 0, 300000}}, {4});
    SimResult r = run_policy(g, sc, "nearest");
    struct Exp {
        SimTime t;
        EventType type;
    };
    const std::vector<Exp> want = {
        {0, EventType::OrderCreated},           {0, EventType::CourierAssigned},
        {120000, EventType::CourierArrivedRestaurant}, {300000, EventType::FoodReady},
        {300000, EventType::OrderPickedUp},     {420000, EventType::OrderDelivered},
        {420000, EventType::CourierAvailable},
    };
    REQUIRE(r.log.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        CHECK(r.log[i].seq == i);
        CHECK(r.log[i].timestamp == want[i].t);
        CHECK(r.log[i].type == want[i].type);
    }
    CHECK(r.log[1].travel_ms == 120000);
    CHECK(r.log[1].distance_mm == 2 * 191000);
    CHECK(r.log[4].travel_ms == 120000);
    CHECK(r.metrics.orders_delivered == 1);
    CHECK(r.metrics.avg_delivery_min == doctest::Approx(7.0));
    CHECK(r.final_state.couriers[0].location == 4);
    CHECK(r.final_state.couriers[0].status == CourierStatus::Idle);
    CHECK(r.final_state.couriers[0].busy_ms == 420000);
    CHECK(r.metrics.total_distance_km == doctest::Approx(4 * 0.191));
}

TEST_CASE("simulator: food ready before courier arrives => pickup on arrival") {
    Graph g = fixtures::line(5);
    Scenario sc = line_scenario(g, {{0, 0, 0, 0, 60000}}, {4});  // ready at 1 min, courier arrives at 2
    SimResult r = run_policy(g, sc, "greedy_eta");
    REQUIRE(r.log.size() == 7);
    CHECK(r.log[2].type == EventType::FoodReady);
    CHECK(r.log[2].timestamp == 60000);
    CHECK(r.log[3].type == EventType::CourierArrivedRestaurant);
    CHECK(r.log[4].type == EventType::OrderPickedUp);
    CHECK(r.log[4].timestamp == 120000);
}

TEST_CASE("simulator: orders queue when no courier is free (FIFO)") {
    Graph g = fixtures::line(5);
    // Three simultaneous orders, one courier: served strictly one after another.
    Scenario sc = line_scenario(g, {{0, 0, 0, 0, 60000}, {1, 0, 1, 0, 60000}, {2, 0, 2, 0, 60000}}, {4, 3, 1});
    SimResult r = run_policy(g, sc, "nearest");
    const auto& o = r.final_state.orders;
    REQUIRE(r.metrics.orders_delivered == 3);
    CHECK(o[0].assigned_at == 0);
    // Order 1 is assigned exactly when the courier becomes available after order 0.
    CHECK(o[1].assigned_at == o[0].delivered_at);
    CHECK(o[2].assigned_at == o[1].delivered_at);
    CHECK(o[1].assigned_at > 0);
    CHECK(r.final_state.couriers[0].deliveries == 3);
}

TEST_CASE("simulator: simulation end leaves undelivered orders") {
    Graph g = fixtures::line(5);
    Scenario sc = line_scenario(g, {{0, 0, 0, 0, 60000}, {1, 0, 1, 0, 60000}}, {4, 4});
    sc.end_time = 300000;  // only the first delivery (done at 4 min) fits
    SimResult r = run_policy(g, sc, "nearest");
    CHECK(r.metrics.orders_delivered == 1);
    CHECK(r.metrics.orders_undelivered == 1);
    for (const auto& e : r.log) CHECK(e.timestamp <= sc.end_time);
}

TEST_CASE("simulator: generated scenario on a grid - invariants, all event types, determinism") {
    Graph g = fixtures::grid(15, 15);
    ScenarioParams p;
    p.couriers = 8;
    p.orders = 120;
    p.order_window_ms = 2 * 3600000;
    p.max_delivery_km = 2.0;
    p.seed = 7;
    for (const char* traffic : {"free", "moderate", "heavy"}) {
        p.traffic = traffic;
        Scenario sc = generate_scenario(g, p);
        Graph tg = apply_traffic(g, parse_traffic(traffic), p.seed);
        for (const char* policy : {"nearest", "greedy_eta"}) {
            SimResult a = run_policy(tg, sc, policy);
            SimResult b = run_policy(tg, sc, policy);
            CHECK(a.final_state_hash == b.final_state_hash);
            REQUIRE(a.log.size() == b.log.size());
            for (std::size_t i = 0; i < a.log.size(); ++i) {
                REQUIRE(log_event_to_json(a.log[i], true) == log_event_to_json(b.log[i], true));
            }
            std::set<EventType> types;
            std::set<std::uint32_t> delivered;
            SimTime last = 0;
            for (const auto& e : a.log) {
                types.insert(e.type);
                CHECK(e.timestamp >= last);
                last = e.timestamp;
                if (e.type == EventType::OrderDelivered) CHECK(delivered.insert(e.order).second);  // never twice
            }
            CHECK(types.size() == kEventTypeCount);
            CHECK(a.metrics.orders_delivered <= a.metrics.orders_created);
            CHECK(a.metrics.orders_delivered > 0);
            CHECK(a.metrics.courier_utilization > 0.0);
            CHECK(a.metrics.courier_utilization <= 1.0);
        }
    }
}

TEST_CASE("simulator: routing algorithm choice does not change costs") {
    Graph g = fixtures::grid(12, 12);
    ScenarioParams p;
    p.couriers = 5;
    p.orders = 60;
    p.max_delivery_km = 2.0;
    p.seed = 3;
    Scenario sc = generate_scenario(g, p);
    SimResult a = run_policy(g, sc, "greedy_eta", Algorithm::Dijkstra);
    SimResult b = run_policy(g, sc, "greedy_eta", Algorithm::AStar);
    SimResult c = run_policy(g, sc, "greedy_eta", Algorithm::Bidirectional);
    // Equal-cost alternative paths may differ in length, but every timestamp
    // depends only on costs, which are exact for all three.
    REQUIRE(a.log.size() == b.log.size());
    REQUIRE(a.log.size() == c.log.size());
    for (std::size_t i = 0; i < a.log.size(); ++i) {
        CHECK(a.log[i].timestamp == b.log[i].timestamp);
        CHECK(a.log[i].timestamp == c.log[i].timestamp);
        CHECK(a.log[i].travel_ms == b.log[i].travel_ms);
        CHECK(a.log[i].travel_ms == c.log[i].travel_ms);
    }
}

TEST_CASE("simulator: state hash is sensitive to state changes") {
    SimState s;
    s.couriers.resize(2);
    s.orders.resize(2);
    const auto h0 = state_hash(s);
    s.couriers[1].distance_mm = 1;
    CHECK(state_hash(s) != h0);
    s.couriers[1].distance_mm = 0;
    CHECK(state_hash(s) == h0);
    s.orders[0].ready_at = 5;
    CHECK(state_hash(s) != h0);
}

TEST_CASE("simulator: scenario generation is deterministic and round-trips through JSON") {
    Graph g = fixtures::grid(10, 10);
    ScenarioParams p;
    p.couriers = 4;
    p.orders = 50;
    p.seed = 11;
    p.max_delivery_km = 1.5;
    Scenario a = generate_scenario(g, p);
    Scenario b = generate_scenario(g, p);
    CHECK(a.hash() == b.hash());
    Scenario c = scenario_from_json(scenario_to_json(a));
    CHECK(c.hash() == a.hash());
    p.seed = 12;
    CHECK(generate_scenario(g, p).hash() != a.hash());
    for (std::size_t i = 1; i < a.orders.size(); ++i) CHECK(a.orders[i].created_at >= a.orders[i - 1].created_at);
    // Tampering is detected.
    std::string text = scenario_to_json(a);
    const auto pos = text.find("\"graph_nodes\":100,");
    REQUIRE(pos != std::string::npos);
    text.replace(pos, 18, "\"graph_nodes\":101,");
    CHECK_THROWS(scenario_from_json(text));
}

TEST_CASE("simulator: traffic multipliers are >= 1 and documented ranges hold") {
    Graph g = fixtures::grid(20, 20);
    for (auto t : {TrafficScenario::FreeFlow, TrafficScenario::Moderate, TrafficScenario::Heavy}) {
        Graph tg = apply_traffic(g, t, 99);
        CHECK(validate(tg).empty());
        const TrafficProfile p = traffic_profile(t);
        for (std::uint32_t e = 0; e < g.num_edges(); ++e) {
            CHECK(tg.weights[e] >= g.weights[e]);
            // residential (class 11) is a local road
            CHECK(static_cast<double>(tg.weights[e]) <= std::ceil(g.weights[e] * (p.local_max_permille / 1000.0)) + 1);
        }
    }
    CHECK(graphs_equal(apply_traffic(g, TrafficScenario::FreeFlow, 1), g));
}
