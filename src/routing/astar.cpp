#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "dispatchlab/geo.hpp"
#include "dispatchlab/counters.hpp"
#include "dispatchlab/routing.hpp"

namespace dispatchlab {

namespace {
// Float unit vectors carry ~1e-7 relative error; this margin (metres) absorbs it
// so the chord bound never exceeds the true chord.
constexpr double kChordSlackM = 3.0;
}  // namespace

std::shared_ptr<const UnitVectors> make_unit_vectors(const Graph& g) {
    auto v = std::make_shared<UnitVectors>(static_cast<std::size_t>(g.num_nodes()) * 3);
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    for (NodeId i = 0; i < g.num_nodes(); ++i) {
        const double la = g.lat[i] * kDeg, lo = g.lon[i] * kDeg;
        (*v)[3 * i] = static_cast<float>(std::cos(la) * std::cos(lo));
        (*v)[3 * i + 1] = static_cast<float>(std::cos(la) * std::sin(lo));
        (*v)[3 * i + 2] = static_cast<float>(std::sin(la));
    }
    return v;
}

AStar::AStar(const Graph& g, Heuristic hz, std::shared_ptr<const UnitVectors> unit)
    : g_(g), vmax_kmh_(g.meta.max_speed_kmh), hz_(hz), unit_(std::move(unit)), ws_(g.num_nodes()) {
    if (vmax_kmh_ == 0 && g.num_nodes() > 0) {
        throw std::invalid_argument("AStar: graph.meta.max_speed_kmh must be set (v_max)");
    }
    ms_per_m_ = 3600.0 / static_cast<double>(vmax_kmh_ ? vmax_kmh_ : 1);
    if (hz_ == Heuristic::Chord && !unit_) unit_ = make_unit_vectors(g);
}

// Chord: |p_v - p_t| * R <= great-circle distance <= length of any path, minus a
// slack covering float rounding, so the bound stays admissible.
Dist AStar::heuristic(NodeId v, NodeId t) const {
    if (hz_ == Heuristic::Haversine) {
        return lower_bound_ms(haversine_m(g_.lat[v], g_.lon[v], g_.lat[t], g_.lon[t]), vmax_kmh_);
    }
    const float* p = unit_->data() + 3 * static_cast<std::size_t>(v);
    const float* q = unit_->data() + 3 * static_cast<std::size_t>(t);
    const double dx = static_cast<double>(p[0]) - q[0], dy = static_cast<double>(p[1]) - q[1],
                 dz = static_cast<double>(p[2]) - q[2];
    const double m = std::sqrt(dx * dx + dy * dy + dz * dz) * kEarthRadiusM - kChordSlackM;
    return m <= 0.0 ? 0 : static_cast<Dist>(m * ms_per_m_);
}

// Admissibility: for every edge, w(e) = ceil(len(e) * 3600 / s(e)) >= len(e) * 3600 / v_max,
// and the geodesic distance to t is <= the sum of edge lengths of any path to t
// (triangle inequality on the sphere). Traffic multipliers are >= 1 and only
// increase weights. Hence h(n) <= true remaining cost.
RouteResult AStar::query(NodeId s, NodeId t, bool with_path, std::vector<NodeId>* trace) {
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
        if (trace) trace->push_back(u);
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
