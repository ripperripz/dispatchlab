#include <set>

#include "dispatchlab/scc.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

namespace {

Graph unit_graph(std::uint32_t n, const std::vector<std::pair<NodeId, NodeId>>& arcs) {
    std::vector<EdgeInput> e;
    for (auto [u, v] : arcs) e.push_back({u, v, 1, 1, 0});
    return build_csr(n, e);
}

std::vector<char> reach(const Graph& g, NodeId s) {
    std::vector<char> vis(g.num_nodes(), 0);
    std::vector<NodeId> st{s};
    vis[s] = 1;
    while (!st.empty()) {
        NodeId u = st.back();
        st.pop_back();
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) {
            if (!vis[g.targets[e]]) {
                vis[g.targets[e]] = 1;
                st.push_back(g.targets[e]);
            }
        }
    }
    return vis;
}

}  // namespace

TEST_CASE("scc: CLRS figure 22.9 graph has 4 known components") {
    // a b c d e f g h = 0..7
    Graph g = unit_graph(8, {{0, 1}, {1, 2}, {1, 4}, {1, 5}, {2, 3}, {2, 6}, {3, 2}, {3, 7},
                             {4, 0}, {4, 5}, {5, 6}, {6, 5}, {6, 7}, {7, 7}});
    SccResult r = strongly_connected_components(g);
    CHECK(r.num_components() == 4);
    auto same = [&](NodeId a, NodeId b) { return r.component[a] == r.component[b]; };
    CHECK(same(0, 1));
    CHECK(same(1, 4));
    CHECK(same(2, 3));
    CHECK(same(5, 6));
    CHECK_FALSE(same(0, 2));
    CHECK_FALSE(same(2, 5));
    CHECK_FALSE(same(5, 7));
    CHECK(r.sizes[r.largest()] == 3);
    Graph big = largest_scc_subgraph(g);
    CHECK(big.num_nodes() == 3);
    CHECK(is_strongly_connected(big));
}

TEST_CASE("scc: empty and singleton graphs") {
    CHECK(strongly_connected_components(unit_graph(0, {})).num_components() == 0);
    SccResult one = strongly_connected_components(unit_graph(1, {}));
    CHECK(one.num_components() == 1);
}

TEST_CASE("scc: iterative implementation handles 1,000,000-node paths") {
    const std::uint32_t n = 1'000'000;
    std::vector<std::pair<NodeId, NodeId>> chain;
    for (NodeId i = 0; i + 1 < n; ++i) chain.push_back({i, i + 1});
    Graph path = unit_graph(n, chain);
    CHECK(strongly_connected_components(path).num_components() == n);  // a DAG
    chain.push_back({n - 1, 0});
    Graph cycle = unit_graph(n, chain);
    SccResult r = strongly_connected_components(cycle);
    CHECK(r.num_components() == 1);
    CHECK(r.sizes[0] == n);
}

TEST_CASE("scc: agrees with brute-force mutual reachability on random graphs") {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        Rng rng(seed);
        const auto n = static_cast<std::uint32_t>(rng.range(5, 60));
        std::vector<std::pair<NodeId, NodeId>> arcs;
        const auto m = rng.range(0, 3 * n);
        for (std::int64_t i = 0; i < m; ++i) {
            arcs.push_back({static_cast<NodeId>(rng.below(n)), static_cast<NodeId>(rng.below(n))});
        }
        Graph g = unit_graph(n, arcs);
        SccResult r = strongly_connected_components(g);
        std::vector<std::vector<char>> R;
        for (NodeId u = 0; u < n; ++u) R.push_back(reach(g, u));
        for (NodeId u = 0; u < n; ++u)
            for (NodeId v = 0; v < n; ++v) {
                const bool mutual = R[u][v] && R[v][u];
                REQUIRE(mutual == (r.component[u] == r.component[v]));
            }
        Graph big = largest_scc_subgraph(g);
        CHECK(is_strongly_connected(big));
        CHECK(big.num_nodes() == r.sizes[r.largest()]);
    }
}
