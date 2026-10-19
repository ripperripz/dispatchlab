#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/simulator.hpp"
#include "dispatchlab/geo.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

namespace {

// Restaurant R at node 0. Courier A at node 1 sits 100 m from R geodesically
// but across a "river": the only road from 1 goes around via 3 and 4 (slow).
// Courier B at node 2 is 400 m away with a direct road. Customer at node 5.
Graph river() {
    std::vector<double> lat = {30.7000, 30.7009, 30.7036, 30.7100, 30.7100, 30.6990};
    std::vector<double> lon = {76.7800, 76.7800, 76.7800, 76.7900, 76.7700, 76.7800};
    std::vector<EdgeInput> e;
    auto link = [&](NodeId a, NodeId b, std::uint32_t kmh) {
        const double m = haversine_m(lat[a], lon[a], lat[b], lon[b]);
        const Weight w = travel_time_ms(m, kmh);
        e.push_back({a, b, w, static_cast<std::uint32_t>(m * 1000), 11});
        e.push_back({b, a, w, static_cast<std::uint32_t>(m * 1000), 11});
    };
    link(1, 3, 20);
    link(3, 4, 20);
    link(4, 0, 20);
    link(2, 0, 20);
    link(0, 5, 20);
    Graph g = build_csr(6, e, lat, lon);
    g.meta.max_speed_kmh = 20;
    return g;
}

}  // namespace

TEST_CASE("dispatch: nearest picks the geodesically closest available courier") {
    Graph g = fixtures::line(6);
    NearestPolicy p(g);
    DispatchRequest req{0, 3, 5, 0, 0};
    CHECK(p.choose(req, {{0, 0}, {1, 5}, {2, 4}}) == 2);  // node 4 is one hop from node 3
}

TEST_CASE("dispatch: nearest breaks exact ties by lowest courier id") {
    Graph g = fixtures::line(6);
    NearestPolicy p(g);
    DispatchRequest req{0, 2, 5, 0, 0};
    CHECK(p.choose(req, {{3, 4}, {7, 4}}) == 3);
    CHECK_THROWS(p.choose(req, {}));
}

TEST_CASE("dispatch: nearest ignores road topology, greedy eta does not") {
    Graph g = river();
    NearestPolicy nearest(g);
    GreedyEtaPolicy eta(g);
    DispatchRequest req{0, 0, 5, 0, 0};
    const std::vector<CourierCandidate> avail = {{0, 1}, {1, 2}};
    CHECK(nearest.choose(req, avail) == 0);  // A: 100 m as the crow flies
    CHECK(eta.choose(req, avail) == 1);      // B: actually reaches R first by road
    const DecisionInfo& d = eta.last_decision();
    Dijkstra dij = make_dijkstra(g);
    CHECK(d.to_restaurant_ms == dij.query(2, 0).cost);
    CHECK(d.restaurant_to_customer_ms == dij.query(0, 5).cost);
    CHECK(d.eta == static_cast<SimTime>(d.to_restaurant_ms + d.restaurant_to_customer_ms));
}

TEST_CASE("dispatch: greedy eta formula max(now + T(c,r), t_ready) + T(r,d)") {
    Graph g = fixtures::line(6);  // 1 min per edge
    GreedyEtaPolicy eta(g);
    // Restaurant at 2, customer at 5 (T(r,d) = 3 min). Courier 0 at node 0 (2 min),
    // courier 1 at node 4 (2 min). Food ready at t = 10 min => both ETAs are
    // max(now+2, 10) + 3 = 13 min; tie on T(c,r) too => lowest id.
    DispatchRequest req{0, 2, 5, 60000, 600000};
    CHECK(eta.choose(req, {{0, 0}, {1, 4}}) == 0);
    CHECK(eta.last_decision().eta == 600000 + 180000);
    // Courier 1 at node 3 (1 min): equal ETA (food not ready), smaller T(c,r) wins.
    CHECK(eta.choose(req, {{0, 0}, {1, 3}}) == 1);
    // Food ready immediately: ETA = now + T(c,r) + T(r,d); closer courier wins.
    DispatchRequest now_ready{0, 2, 5, 60000, 0};
    CHECK(eta.choose(now_ready, {{5, 0}, {9, 3}}) == 9);
    CHECK(eta.last_decision().eta == 60000 + 60000 + 180000);
}

TEST_CASE("dispatch: greedy eta only considers available couriers") {
    Graph g = fixtures::line(6);
    GreedyEtaPolicy eta(g);
    DispatchRequest req{0, 2, 5, 0, 0};
    // The courier at node 2 is not in the available list, so it cannot be picked.
    CHECK(eta.choose(req, {{4, 5}}) == 4);
    CHECK_THROWS(eta.choose(req, {}));
    CHECK(make_policy("nearest", g)->name() == "nearest");
    CHECK(make_policy("greedy_eta", g)->name() == "greedy_eta");
    CHECK_THROWS(make_policy("min_cost_flow", g));
}

namespace {
const char* kEtaVariants[] = {"greedy_eta", "greedy_eta_pruned", "greedy_eta_cch", "greedy_eta_bounded"};
}

TEST_CASE("dispatch: greedy eta strategies make identical decisions on random requests") {
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        Graph g = seed % 2 ? fixtures::grid(16, 16) : fixtures::random_geometric(400, 3, seed);
        CCH cch(g);
        GreedyEtaPolicy full(g, EtaStrategy::Full), pruned(g, EtaStrategy::Pruned), viacch(g, EtaStrategy::Cch, &cch),
            bounded(g, EtaStrategy::Bounded, &cch);
        Rng rng(seed);
        for (int i = 0; i < 300; ++i) {
            const auto r = static_cast<NodeId>(rng.below(g.num_nodes()));
            const auto d = static_cast<NodeId>(rng.below(g.num_nodes()));
            if (g.num_nodes() && make_dijkstra(g).query(r, d, false).cost == kInfDist) continue;
            const SimTime now = rng.range(0, 3600000);
            // Mix of "food already ready" and "food ready much later" (tie-heavy) requests.
            const SimTime ready = rng.below(2) ? now : now + rng.range(0, 1800000);
            std::vector<CourierCandidate> av;
            const auto k = rng.range(1, 40);
            for (std::int64_t j = 0; j < k; ++j) {
                // Some couriers share nodes to exercise the id tie-break.
                const NodeId loc = j > 0 && rng.below(5) == 0 ? av.back().location : static_cast<NodeId>(rng.below(g.num_nodes()));
                av.push_back({static_cast<std::uint32_t>(j * 3 + 1), loc});
            }
            DispatchRequest req{0, r, d, now, ready};
            std::uint32_t chosen[4];
            SimTime eta[4];
            bool ok[4];
            GreedyEtaPolicy* pols[4] = {&full, &pruned, &viacch, &bounded};
            for (int p = 0; p < 4; ++p) {
                try {
                    chosen[p] = pols[p]->choose(req, av);
                    eta[p] = pols[p]->last_decision().eta;
                    ok[p] = true;
                } catch (const std::runtime_error&) {
                    ok[p] = false;  // no courier can reach the restaurant
                }
            }
            for (int p = 1; p < 4; ++p) {
                REQUIRE(ok[p] == ok[0]);
                if (ok[0]) {
                    REQUIRE(chosen[p] == chosen[0]);
                    REQUIRE(eta[p] == eta[0]);
                }
            }
        }
        // The decision-aware strategies must not do more exact work than the baseline.
        CHECK(pruned.nodes_settled() <= full.nodes_settled());
        CHECK(bounded.exact_distances() <= viacch.exact_distances());
    }
}

TEST_CASE("dispatch: greedy eta strategies give identical simulations (same log, same hash)") {
    Graph g = fixtures::grid(14, 14);
    ScenarioParams p;
    p.couriers = 10;
    p.orders = 120;
    p.order_window_ms = 90 * 60000;  // busy enough to queue orders
    p.max_delivery_km = 2.0;
    p.seed = 21;
    Scenario sc = generate_scenario(g, p);
    std::vector<std::string> logs;
    std::vector<std::uint64_t> hashes;
    for (const char* name : kEtaVariants) {
        auto pol = make_policy(name, g);
        CHECK(pol->name() == name);
        Simulator sim(g, sc, *pol, Algorithm::AStar);
        SimResult r = sim.run();
        std::string text;
        for (const auto& e : r.log) text += log_event_to_json(e, false) + "\n";
        logs.push_back(text);
        hashes.push_back(r.final_state_hash);
    }
    for (std::size_t i = 1; i < hashes.size(); ++i) {
        CHECK(hashes[i] == hashes[0]);
        CHECK(logs[i] == logs[0]);
    }
}
