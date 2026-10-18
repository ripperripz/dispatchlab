#include <algorithm>
#include <stdexcept>

#include "dispatchlab/geo.hpp"
#include "dispatchlab/counters.hpp"
#include "dispatchlab/routing.hpp"

namespace dispatchlab {

AStar::AStar(const Graph& g) : g_(g), vmax_kmh_(g.meta.max_speed_kmh), ws_(g.num_nodes()) {
    if (vmax_kmh_ == 0 && g.num_nodes() > 0) {
        throw std::invalid_argument("AStar: graph.meta.max_speed_kmh must be set (v_max)");
    }
}

Dist AStar::heuristic(NodeId v, NodeId t) const {
    return lower_bound_ms(haversine_m(g_.lat[v], g_.lon[v], g_.lat[t], g_.lon[t]), vmax_kmh_);
}

// Admissibility: for every edge, w(e) = ceil(len(e) * 3600 / s(e)) >= len(e) * 3600 / v_max,
// and the geodesic distance to t is <= the sum of edge lengths of any path to t
// (triangle inequality on the sphere). Traffic multipliers are >= 1 and only
// increase weights. Hence h(n) <= true remaining cost.
RouteResult AStar::query(NodeId s, NodeId t, bool with_path) {
    const std::uint32_t n = g_.num_nodes();
    if (s >= n || t >= n) throw std::out_of_range("AStar: node id out of range");
    count_routing_call();
    RouteResult r;
    ws_.reset();
    heap_.clear();
    ws_.set(s, 0, kInvalidNode);
    heap_.push({heuristic(s, t), s});
    ++r.stats.pushes;
    while (!heap_.empty()) {
        const HeapItem top = heap_.take();
        const NodeId u = top.node;
        const Dist g_u = ws_.dist(u);
        // Stale if the key no longer matches g(u) + h(u).
        if (top.key != g_u + heuristic(u, t)) continue;
        ++r.stats.settled;
        if (u == t) {
            r.cost = g_u;
            break;
        }
        const std::uint32_t end = g_.offsets[u + 1];
        for (std::uint32_t e = g_.offsets[u]; e < end; ++e) {
            const NodeId v = g_.targets[e];
            const Dist nd = sat_add(g_u, g_.weights[e]);
            if (nd < ws_.dist(v)) {  // also re-opens already expanded nodes
                ws_.set(v, nd, u);
                heap_.push({nd + heuristic(v, t), v});
                ++r.stats.relaxed;
                ++r.stats.pushes;
            }
        }
    }
    if (with_path && r.reachable()) {
        for (NodeId v = t; v != kInvalidNode; v = ws_.parent(v)) r.path.push_back(v);
        std::reverse(r.path.begin(), r.path.end());
    }
    return r;
}

}  // namespace dispatchlab
