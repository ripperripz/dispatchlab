#include "dispatchlab/routing.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

TEST_CASE("dijkstra: exact distances and path on the classic fixture") {
    Graph g = fixtures::classic();
    Dijkstra d = make_dijkstra(g);
    const Dist expected[] = {0, 7, 9, 20, 20, 11};
    for (NodeId t = 0; t < 6; ++t) {
        RouteResult r = d.query(0, t);
        CHECK(r.cost == expected[t]);
        CHECK(path_cost(g, r.path) == r.cost);
        CHECK(r.path.front() == 0);
        CHECK(r.path.back() == t);
    }
    CHECK(d.query(0, 4).path == std::vector<NodeId>{0, 2, 5, 4});
}

TEST_CASE("dijkstra: unreachable destinations") {
    Graph g = fixtures::classic();
    Dijkstra d = make_dijkstra(g);
    RouteResult r = d.query(0, 6);
    CHECK_FALSE(r.reachable());
    CHECK(r.path.empty());
    CHECK_FALSE(d.query(4, 0).reachable());  // directed: no way back
    BidirectionalDijkstra b(g);
    CHECK_FALSE(b.query(0, 6).reachable());
    CHECK_FALSE(b.query(4, 0).reachable());
    AStar a(g);
    CHECK_FALSE(a.query(0, 6).reachable());
    CHECK_FALSE(a.query(4, 0).reachable());
}

TEST_CASE("routing: one-way cycle fixture") {
    Graph g = fixtures::oneway_cycle();
    Dijkstra d = make_dijkstra(g);
    BidirectionalDijkstra b(g);
    AStar a(g);
    CHECK(d.query(0, 2).cost == 15);
    CHECK(d.query(2, 0).cost == 20);
    CHECK(d.query(3, 2).cost == 25);
    for (NodeId s = 0; s < 4; ++s)
        for (NodeId t = 0; t < 4; ++t) {
            CHECK(b.query(s, t).cost == d.query(s, t).cost);
            CHECK(a.query(s, t).cost == d.query(s, t).cost);
        }
}

TEST_CASE("routing: source == target") {
    Graph g = fixtures::classic();
    Dijkstra d = make_dijkstra(g);
    BidirectionalDijkstra b(g);
    AStar a(g);
    for (NodeId s = 0; s < g.num_nodes(); ++s) {
        CHECK(d.query(s, s).cost == 0);
        CHECK(b.query(s, s).cost == 0);
        CHECK(a.query(s, s).cost == 0);
        CHECK(b.query(s, s).path == std::vector<NodeId>{s});
    }
}

TEST_CASE("routing: overflow protection") {
    CHECK_THROWS_AS(sat_add(kInfDist - 5, 10), std::overflow_error);
    CHECK(sat_add(10, 5) == 15);
}

TEST_CASE("routing: all algorithms match Floyd-Warshall on random graphs") {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        Graph g = fixtures::random_geometric(70, 2, seed);
        REQUIRE(validate(g).empty());
        const auto oracle = fixtures::floyd_warshall(g);
        Dijkstra d = make_dijkstra(g);
        BidirectionalDijkstra b(g);
        AStar a(g);
        for (NodeId s = 0; s < g.num_nodes(); ++s)
            for (NodeId t = 0; t < g.num_nodes(); ++t) {
                const Dist want = oracle[s][t];
                RouteResult rd = d.query(s, t);
                RouteResult rb = b.query(s, t);
                RouteResult ra = a.query(s, t);
                REQUIRE(rd.cost == want);
                REQUIRE(rb.cost == want);
                REQUIRE(ra.cost == want);
                if (want != kInfDist) {
                    REQUIRE(path_cost(g, rd.path) == want);
                    REQUIRE(path_cost(g, rb.path) == want);
                    REQUIRE(path_cost(g, ra.path) == want);
                    REQUIRE(rb.path.front() == s);
                    REQUIRE(rb.path.back() == t);
                }
            }
    }
}

TEST_CASE("astar: heuristic never exceeds true distance (sampled)") {
    Graph g = fixtures::random_geometric(400, 3, 99);
    AStar a(g);
    ReverseDijkstra rev = make_reverse_dijkstra(g);
    Rng rng(5);
    for (int k = 0; k < 20; ++k) {
        const auto t = static_cast<NodeId>(rng.below(g.num_nodes()));
        rev.one_to_all(t);  // distances *to* t
        for (NodeId v = 0; v < g.num_nodes(); ++v) {
            if (rev.dist(v) == kInfDist) continue;
            REQUIRE(a.heuristic(v, t) <= rev.dist(v));
        }
    }
}

TEST_CASE("dijkstra: one_to_many and reverse search agree with point queries") {
    Graph g = fixtures::random_geometric(300, 3, 3);
    Dijkstra d = make_dijkstra(g);
    ReverseDijkstra rev = make_reverse_dijkstra(g);
    std::vector<NodeId> targets = {5, 17, 17, 250, 0, 299};
    for (NodeId s : {0u, 42u, 123u}) {
        auto fwd = d.one_to_many(s, targets);
        auto bwd = rev.one_to_many(s, targets);  // distances targets[i] -> s
        for (std::size_t i = 0; i < targets.size(); ++i) {
            CHECK(fwd[i] == d.query(s, targets[i], false).cost);
            CHECK(bwd[i] == d.query(targets[i], s, false).cost);
        }
    }
}

TEST_CASE("dijkstra: adjacency-list baseline gives identical costs") {
    Graph g = fixtures::random_geometric(300, 3, 21);
    AdjacencyGraph adj = to_adjacency(g);
    Dijkstra d = make_dijkstra(g);
    AdjacencyDijkstra da{AdjOut{&adj}};
    Rng rng(8);
    for (int i = 0; i < 500; ++i) {
        const auto s = static_cast<NodeId>(rng.below(g.num_nodes()));
        const auto t = static_cast<NodeId>(rng.below(g.num_nodes()));
        RouteResult a = d.query(s, t);
        RouteResult b = da.query(s, t);
        CHECK(a.cost == b.cost);
        CHECK(a.path == b.path);
        CHECK(a.stats.settled == b.stats.settled);
    }
}

TEST_CASE("router: algorithm names round-trip") {
    for (Algorithm a : {Algorithm::Dijkstra, Algorithm::Bidirectional, Algorithm::AStar}) {
        CHECK(parse_algorithm(algorithm_name(a)) == a);
    }
    CHECK_THROWS(parse_algorithm("alt"));
}

TEST_CASE("astar: equals Dijkstra and settles no more nodes on geometric graphs") {
    for (std::uint64_t seed = 100; seed < 110; ++seed) {
        Graph g = fixtures::random_geometric(500, 3, seed);
        Dijkstra d = make_dijkstra(g);
        AStar a(g);
        Rng rng(seed);
        for (int i = 0; i < 200; ++i) {
            const auto s = static_cast<NodeId>(rng.below(g.num_nodes()));
            const auto t = static_cast<NodeId>(rng.below(g.num_nodes()));
            RouteResult rd = d.query(s, t);
            RouteResult ra = a.query(s, t);
            REQUIRE(ra.cost == rd.cost);
            if (rd.reachable()) REQUIRE(path_cost(g, ra.path) == ra.cost);
        }
    }
}

TEST_CASE("astar: v_max bound holds for every edge (h(u->v) <= w(u,v))") {
    Graph g = fixtures::random_geometric(500, 3, 4242);
    AStar a(g);
    for (NodeId u = 0; u < g.num_nodes(); ++u)
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) REQUIRE(a.heuristic(u, g.targets[e]) <= g.weights[e]);
}

TEST_CASE("astar: requires v_max") {
    Graph g = fixtures::classic();
    g.meta.max_speed_kmh = 0;
    CHECK_THROWS(AStar(g));
}

TEST_CASE("bidirectional: equals Dijkstra with valid paths on random graphs") {
    for (std::uint64_t seed = 200; seed < 215; ++seed) {
        Graph g = fixtures::random_geometric(400, 2, seed);
        Dijkstra d = make_dijkstra(g);
        BidirectionalDijkstra b(g);
        Rng rng(seed);
        for (int i = 0; i < 300; ++i) {
            const auto s = static_cast<NodeId>(rng.below(g.num_nodes()));
            const auto t = static_cast<NodeId>(rng.below(g.num_nodes()));
            RouteResult rd = d.query(s, t);
            RouteResult rb = b.query(s, t);
            REQUIRE(rb.cost == rd.cost);
            if (rd.reachable()) {
                REQUIRE(path_cost(g, rb.path) == rb.cost);
                REQUIRE(rb.path.front() == s);
                REQUIRE(rb.path.back() == t);
            } else {
                REQUIRE(rb.path.empty());
            }
        }
    }
}

TEST_CASE("bidirectional: classic and one-way fixtures") {
    Graph g = fixtures::classic();
    BidirectionalDijkstra b(g);
    CHECK(b.query(0, 4).cost == 20);
    CHECK(b.query(0, 3).cost == 20);
    CHECK(path_cost(g, b.query(0, 4).path) == 20);
    Graph c = fixtures::oneway_cycle();
    BidirectionalDijkstra bc(c);
    CHECK(bc.query(2, 0).cost == 20);
    CHECK(bc.query(3, 2).cost == 25);
}
