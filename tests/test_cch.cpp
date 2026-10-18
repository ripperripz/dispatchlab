#include "dispatchlab/cch.hpp"
#include "dispatchlab/traffic.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

namespace {

void check_against_dijkstra(const Graph& g, CCH& cch, int pairs, std::uint64_t seed) {
    Dijkstra d = make_dijkstra(g);
    Rng rng(seed);
    for (int i = 0; i < pairs; ++i) {
        const auto s = static_cast<NodeId>(rng.below(g.num_nodes()));
        const auto t = static_cast<NodeId>(rng.below(g.num_nodes()));
        const RouteResult a = d.query(s, t);
        const RouteResult b = cch.query(s, t);
        REQUIRE(b.cost == a.cost);
        if (a.reachable()) {
            REQUIRE(path_cost(g, b.path) == b.cost);
            REQUIRE(b.path.front() == s);
            REQUIRE(b.path.back() == t);
        } else {
            REQUIRE(b.path.empty());
        }
    }
}

}  // namespace

TEST_CASE("cch: exact on hand-built fixtures (all pairs)") {
    for (Graph g : {fixtures::classic(), fixtures::oneway_cycle(), fixtures::line(7), fixtures::grid(6, 7)}) {
        CCH cch(g);
        Dijkstra d = make_dijkstra(g);
        for (NodeId s = 0; s < g.num_nodes(); ++s)
            for (NodeId t = 0; t < g.num_nodes(); ++t) {
                const RouteResult b = cch.query(s, t);
                REQUIRE(b.cost == d.query(s, t).cost);
                if (b.reachable()) REQUIRE(path_cost(g, b.path) == b.cost);
            }
    }
}

TEST_CASE("cch: exact on random directed geometric graphs (with unreachable pairs)") {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        Graph g = fixtures::random_geometric(400, 2, seed);
        CCH cch(g);
        check_against_dijkstra(g, cch, 400, seed);
    }
}

TEST_CASE("cch: re-customization for traffic weights stays exact") {
    Graph g = fixtures::grid(18, 18);
    CCH cch(g);
    check_against_dijkstra(g, cch, 300, 1);
    Graph heavy = apply_traffic(g, TrafficScenario::Heavy, 5);
    cch.customize(heavy);
    check_against_dijkstra(heavy, cch, 300, 2);
}

TEST_CASE("cch: every node gets a unique rank") {
    Graph g = fixtures::random_geometric(300, 3, 77);
    CCH cch(g);
    std::vector<char> seen(g.num_nodes(), 0);
    for (NodeId v = 0; v < g.num_nodes(); ++v) {
        REQUIRE(cch.rank_of(v) < g.num_nodes());
        REQUIRE_FALSE(seen[cch.rank_of(v)]);
        seen[cch.rank_of(v)] = 1;
    }
    CHECK(cch.stats().upward_edges >= cch.stats().original_pairs);
}
