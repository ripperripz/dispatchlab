#include <cstdio>
#include <fstream>

#include "dispatchlab/graph.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

TEST_CASE("graph: CSR construction, directed edges and weights") {
    Graph g = fixtures::classic();
    CHECK(g.num_nodes() == 7);
    CHECK(g.num_edges() == 9);
    CHECK(validate(g).empty());
    CHECK(g.edge_weight(0, 1) == 7);
    CHECK(g.edge_weight(1, 0) == kInfDist);  // directed
    CHECK(g.edge_weight(2, 5) == 2);
    CHECK(g.edge_weight(6, 0) == kInfDist);
    // Out-edges of 0 sorted by target.
    std::vector<NodeId> outs(g.targets.begin() + g.out_begin(0), g.targets.begin() + g.out_end(0));
    CHECK(outs == std::vector<NodeId>{1, 2, 5});
    // Reverse CSR: in-edges of 4 come from 3 and 5.
    std::vector<NodeId> ins(g.rev_sources.begin() + g.in_begin(4), g.rev_sources.begin() + g.in_end(4));
    CHECK(ins == std::vector<NodeId>{3, 5});
}

TEST_CASE("graph: self-loops dropped, parallel edges keep minimum, order independent") {
    std::vector<EdgeInput> e = {{0, 1, 5, 1, 0}, {0, 1, 3, 1, 0}, {1, 1, 4, 1, 0}, {1, 2, 2, 1, 0}, {0, 1, 9, 1, 0}};
    Graph a = build_csr(3, e);
    std::reverse(e.begin(), e.end());
    Graph b = build_csr(3, e);
    CHECK(graphs_equal(a, b));
    CHECK(a.num_edges() == 2);
    CHECK(a.edge_weight(0, 1) == 3);
    CHECK(a.edge_weight(1, 1) == kInfDist);
    CHECK(validate(a).empty());
}

TEST_CASE("graph: invalid input rejected") {
    CHECK_THROWS(build_csr(2, {{0, 2, 1, 1, 0}}));
    CHECK_THROWS(build_csr(2, {{0, 1, 0, 1, 0}}));  // zero weight
}

TEST_CASE("graph: validate detects corruption") {
    Graph g = fixtures::classic();
    Graph bad = g;
    bad.targets[0] = 99;
    CHECK_FALSE(validate(bad).empty());
    bad = g;
    bad.weights[3] = 0;
    CHECK_FALSE(validate(bad).empty());
    bad = g;
    bad.rev_weights[0] += 1;
    CHECK_FALSE(validate(bad).empty());
    bad = g;
    bad.meta.max_speed_kmh = 1;
    bad.lengths_mm[0] = 1'000'000;  // 1 km in 7 ms is far above 1 km/h
    CHECK_FALSE(validate(bad).empty());
}

TEST_CASE("graph: binary serialization round trip and checksum") {
    Graph g = fixtures::random_geometric(300, 4, 7);
    g.meta.source = "unit-test";
    const std::string path = fixtures::temp_path("roundtrip.graph");
    save_graph(g, path);
    Graph h = load_graph(path);
    CHECK(graphs_equal(g, h));

    // Flip one payload byte => checksum failure.
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(64);
        char c;
        f.read(&c, 1);
        c = static_cast<char>(c ^ 0x5a);
        f.seekp(64);
        f.write(&c, 1);
    }
    CHECK_THROWS(load_graph(path));
    std::remove(path.c_str());
}

TEST_CASE("graph: adjacency-list baseline holds identical edges") {
    Graph g = fixtures::random_geometric(200, 3, 11);
    AdjacencyGraph a = to_adjacency(g);
    REQUIRE(a.num_nodes() == g.num_nodes());
    std::size_t m = 0;
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        REQUIRE(a.out[u].size() == g.out_end(u) - g.out_begin(u));
        for (std::size_t i = 0; i < a.out[u].size(); ++i) {
            CHECK(a.out[u][i].target == g.targets[g.out_begin(u) + i]);
            CHECK(a.out[u][i].weight == g.weights[g.out_begin(u) + i]);
        }
        m += a.out[u].size();
    }
    CHECK(m == g.num_edges());
    const std::string path = fixtures::temp_path("adj.graph");
    save_graph(g, path);
    AdjacencyGraph b = load_adjacency(path);
    CHECK(b.out.size() == a.out.size());
    std::remove(path.c_str());
}

TEST_CASE("graph: induced subgraph keeps order and edges") {
    Graph g = fixtures::classic();
    std::vector<char> keep = {1, 0, 1, 1, 1, 1, 0};  // drop 1 and 6
    Graph s = induced_subgraph(g, keep);
    CHECK(s.num_nodes() == 5);
    // old 0,2,3,4,5 -> new 0,1,2,3,4 ; old 2->5 (w=2) becomes 1->4
    CHECK(s.edge_weight(1, 4) == 2);
    CHECK(s.edge_weight(0, 1) == 9);
    CHECK(validate(s).empty());
}

TEST_CASE("graph: relabel preserves every distance") {
    Graph g = fixtures::random_geometric(300, 3, 5);
    std::vector<NodeId> perm(g.num_nodes());
    for (NodeId v = 0; v < g.num_nodes(); ++v) perm[v] = v;
    Rng rng(3);
    for (std::size_t i = perm.size() - 1; i > 0; --i) std::swap(perm[i], perm[rng.below(i + 1)]);
    Graph h = relabel(g, perm);
    CHECK(validate(h).empty());
    CHECK(h.num_edges() == g.num_edges());
    for (NodeId u = 0; u < g.num_nodes(); ++u)
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) CHECK(h.edge_weight(perm[u], perm[g.targets[e]]) == g.weights[e]);
    CHECK_THROWS(relabel(g, std::vector<NodeId>(g.num_nodes(), 0)));
}

TEST_CASE("graph: strong connectivity check") {
    CHECK(is_strongly_connected(fixtures::oneway_cycle()));
    CHECK_FALSE(is_strongly_connected(fixtures::classic()));
}
