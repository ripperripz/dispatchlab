#pragma once

// Hand-built graph fixtures with known shortest paths, plus a random geometric
// graph generator whose weights respect the A* v_max bound.

#include <cstdio>
#include <string>
#include <vector>

#include "dispatchlab/geo.hpp"
#include "dispatchlab/graph.hpp"
#include "dispatchlab/rng.hpp"

namespace fixtures {

using namespace dispatchlab;

// Classic 6-node example (plus isolated node 6).
//   0->1:7  0->2:9  0->5:14  1->2:10  1->3:15  2->3:11  2->5:2  3->4:6  5->4:9
// Shortest from 0: d1=7 d2=9 d3=20 d4=20 (0,2,5,4) d5=11, node 6 unreachable.
// All coordinates equal => A* heuristic is 0 (still a valid lower bound).
inline Graph classic() {
    std::vector<EdgeInput> e = {{0, 1, 7, 7, 0},  {0, 2, 9, 9, 0},   {0, 5, 14, 14, 0},
                                {1, 2, 10, 10, 0}, {1, 3, 15, 15, 0}, {2, 3, 11, 11, 0},
                                {2, 5, 2, 2, 0},   {3, 4, 6, 6, 0},   {5, 4, 9, 9, 0}};
    Graph g = build_csr(7, e, std::vector<double>(7, 30.7), std::vector<double>(7, 76.8));
    g.meta.max_speed_kmh = 4;  // length(mm) == weight(ms) => 3.6 km/h implied speed
    return g;
}

// Directed cycle with a one-way shortcut: 0->1->2->3->0 (weight 10 each) and a
// one-way chord 0->2 (weight 15). d(0,2)=15, d(2,0)=20, d(3,2)=25.
inline Graph oneway_cycle() {
    std::vector<EdgeInput> e = {{0, 1, 10, 1, 0}, {1, 2, 10, 1, 0}, {2, 3, 10, 1, 0},
                                {3, 0, 10, 1, 0}, {0, 2, 15, 1, 0}};
    Graph g = build_csr(4, e);
    g.meta.max_speed_kmh = 1;
    return g;
}

// Random directed geometric graph: n nodes scattered in a ~5 km box around
// Chandigarh, each node linked to `k` random near-ish nodes. Weights are the
// geodesic travel time at a random speed <= vmax, times a factor >= 1, so the
// A* heuristic with vmax is admissible. Some edges are one-way, so not every
// pair is reachable.
inline Graph random_geometric(std::uint32_t n, std::uint32_t k, std::uint64_t seed,
                              std::uint32_t vmax = 60) {
    Rng rng(seed);
    std::vector<double> lat(n), lon(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        lat[i] = 30.70 + rng.unit() * 0.045;
        lon[i] = 76.75 + rng.unit() * 0.05;
    }
    std::vector<EdgeInput> edges;
    for (std::uint32_t u = 0; u < n; ++u) {
        for (std::uint32_t j = 0; j < k; ++j) {
            const auto v = static_cast<std::uint32_t>(rng.below(n));
            if (v == u) continue;
            const double m = haversine_m(lat[u], lon[u], lat[v], lon[v]);
            const auto speed = static_cast<std::uint32_t>(rng.range(10, vmax));
            Weight w = travel_time_ms(m, speed);
            w += static_cast<Weight>(rng.below(2000));  // extra delay, keeps w >= bound
            const auto len = static_cast<std::uint32_t>(m * 1000.0);
            edges.push_back({u, v, w, len, 0});
            if (rng.below(100) < 60) edges.push_back({v, u, w, len, 0});  // 40% one-way
        }
    }
    Graph g = build_csr(n, std::move(edges), lat, lon);
    g.meta.max_speed_kmh = vmax;
    return g;
}

// All-pairs shortest paths by Floyd-Warshall (independent oracle, small n only).
inline std::vector<std::vector<Dist>> floyd_warshall(const Graph& g) {
    const std::uint32_t n = g.num_nodes();
    std::vector<std::vector<Dist>> d(n, std::vector<Dist>(n, kInfDist));
    for (std::uint32_t u = 0; u < n; ++u) {
        d[u][u] = 0;
        for (std::uint32_t e = g.offsets[u]; e < g.offsets[u + 1]; ++e) {
            d[u][g.targets[e]] = std::min<Dist>(d[u][g.targets[e]], g.weights[e]);
        }
    }
    for (std::uint32_t k = 0; k < n; ++k)
        for (std::uint32_t i = 0; i < n; ++i) {
            if (d[i][k] == kInfDist) continue;
            for (std::uint32_t j = 0; j < n; ++j) {
                if (d[k][j] == kInfDist) continue;
                d[i][j] = std::min(d[i][j], d[i][k] + d[k][j]);
            }
        }
    return d;
}

// Two-way line 0-1-...-(n-1) along a parallel, every edge exactly `w` ms.
inline Graph line(std::uint32_t n, Weight w = 60000) {
    std::vector<EdgeInput> e;
    std::vector<double> lat(n, 30.70), lon(n);
    for (std::uint32_t i = 0; i < n; ++i) lon[i] = 76.70 + 0.002 * i;  // ~191 m apart
    for (std::uint32_t i = 0; i + 1 < n; ++i) {
        e.push_back({i, i + 1, w, 191000, 11});
        e.push_back({i + 1, i, w, 191000, 11});
    }
    Graph g = build_csr(n, e, lat, lon);
    g.meta.max_speed_kmh = 60;
    g.meta.source = "fixture:line";
    return g;
}

// rows x cols two-way grid (~200 m spacing), class-speed weights at 20 km/h,
// strongly connected.
inline Graph grid(std::uint32_t rows, std::uint32_t cols) {
    const std::uint32_t n = rows * cols;
    std::vector<double> lat(n), lon(n);
    for (std::uint32_t r = 0; r < rows; ++r)
        for (std::uint32_t c = 0; c < cols; ++c) {
            lat[r * cols + c] = 30.70 + 0.0018 * r;
            lon[r * cols + c] = 76.75 + 0.0021 * c;
        }
    std::vector<EdgeInput> e;
    auto link = [&](std::uint32_t a, std::uint32_t b) {
        const double m = haversine_m(lat[a], lon[a], lat[b], lon[b]);
        const Weight w = travel_time_ms(m, 20);
        const auto len = static_cast<std::uint32_t>(m * 1000);
        e.push_back({a, b, w, len, 11});
        e.push_back({b, a, w, len, 11});
    };
    for (std::uint32_t r = 0; r < rows; ++r)
        for (std::uint32_t c = 0; c < cols; ++c) {
            if (c + 1 < cols) link(r * cols + c, r * cols + c + 1);
            if (r + 1 < rows) link(r * cols + c, (r + 1) * cols + c);
        }
    Graph g = build_csr(n, e, lat, lon);
    g.meta.max_speed_kmh = 20;
    g.meta.source = "fixture:grid";
    return g;
}

inline std::string temp_path(const std::string& name) {
    return std::string(P_tmpdir) + "/dispatchlab_test_" + name;
}

}  // namespace fixtures
