#include <algorithm>
#include <stdexcept>

#include "dispatchlab/counters.hpp"
#include "dispatchlab/routing.hpp"

namespace dispatchlab {

namespace {

std::vector<NodeId> unwind(const SearchWorkspace& ws, NodeId t) {
    std::vector<NodeId> path;
    for (NodeId v = t; v != kInvalidNode; v = ws.parent(v)) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

}  // namespace

template <class Access, class Heap>
BasicDijkstra<Access, Heap>::BasicDijkstra(Access access) : access_(access), ws_(access.num_nodes()) {
    target_mark_.assign(access.num_nodes(), 0);
}

template <class Access, class Heap>
RouteResult BasicDijkstra<Access, Heap>::query(NodeId s, NodeId t, bool with_path, std::vector<NodeId>* trace) {
    const std::uint32_t n = access_.num_nodes();
    if (s >= n || t >= n) throw std::out_of_range("Dijkstra: node id out of range");
    count_routing_call();
    RouteResult r;
    ws_.reset();
    heap_.clear();
    ws_.set(s, 0, kInvalidNode);
    heap_.push({0, s});
    ++r.stats.pushes;
    while (!heap_.empty()) {
        const HeapItem top = heap_.take();
        if (top.key != ws_.dist(top.node)) continue;  // stale duplicate
        ++r.stats.settled;
        const NodeId u = top.node;
        if (trace) trace->push_back(u);
        if (u == t) {
            r.cost = top.key;
            break;
        }
        access_.for_each(u, [&](NodeId v, Weight w) {
            const Dist nd = sat_add(top.key, w);
            if (nd < ws_.dist(v)) {
                ws_.set(v, nd, u);
                heap_.push({nd, v});
                ++r.stats.relaxed;
                ++r.stats.pushes;
            }
        });
    }
    if (with_path && r.reachable()) r.path = unwind(ws_, t);
    return r;
}

template <class Access, class Heap>
void BasicDijkstra<Access, Heap>::one_to_all(NodeId s, QueryStats* stats) {
    if (s >= access_.num_nodes()) throw std::out_of_range("Dijkstra: node id out of range");
    count_routing_call();
    ws_.reset();
    heap_.clear();
    ws_.set(s, 0, kInvalidNode);
    heap_.push({0, s});
    QueryStats st;
    while (!heap_.empty()) {
        const HeapItem top = heap_.take();
        if (top.key != ws_.dist(top.node)) continue;
        ++st.settled;
        access_.for_each(top.node, [&](NodeId v, Weight w) {
            const Dist nd = sat_add(top.key, w);
            if (nd < ws_.dist(v)) {
                ws_.set(v, nd, top.node);
                heap_.push({nd, v});
                ++st.relaxed;
            }
        });
    }
    if (stats) *stats = st;
}

template <class Access, class Heap>
std::vector<Dist> BasicDijkstra<Access, Heap>::one_to_many(NodeId s, const std::vector<NodeId>& targets,
                                                     QueryStats* stats) {
    const std::uint32_t n = access_.num_nodes();
    if (s >= n) throw std::out_of_range("Dijkstra: node id out of range");
    if (++mark_epoch_ == 0) {
        std::fill(target_mark_.begin(), target_mark_.end(), 0);
        mark_epoch_ = 1;
    }
    std::uint32_t remaining = 0;
    for (NodeId t : targets) {
        if (t >= n) throw std::out_of_range("Dijkstra: target out of range");
        if (target_mark_[t] != mark_epoch_) {
            target_mark_[t] = mark_epoch_;
            ++remaining;
        }
    }
    count_routing_call();
    ws_.reset();
    heap_.clear();
    ws_.set(s, 0, kInvalidNode);
    heap_.push({0, s});
    QueryStats st;
    while (!heap_.empty() && remaining > 0) {
        const HeapItem top = heap_.take();
        if (top.key != ws_.dist(top.node)) continue;
        ++st.settled;
        if (target_mark_[top.node] == mark_epoch_) {
            target_mark_[top.node] = 0;  // count each distinct target once
            --remaining;
        }
        access_.for_each(top.node, [&](NodeId v, Weight w) {
            const Dist nd = sat_add(top.key, w);
            if (nd < ws_.dist(v)) {
                ws_.set(v, nd, top.node);
                heap_.push({nd, v});
                ++st.relaxed;
            }
        });
    }
    // Every target has been settled (or the search exhausted), so labels are final.
    std::vector<Dist> out;
    out.reserve(targets.size());
    for (NodeId t : targets) out.push_back(ws_.dist(t));
    if (stats) *stats = st;
    return out;
}

template class BasicDijkstra<CsrOut>;
template class BasicDijkstra<CsrIn>;
template class BasicDijkstra<AdjOut>;
template class BasicDijkstra<CsrOut, QuaternaryHeap>;
template class BasicDijkstra<CsrOut, RadixHeap<HeapItem>>;
template class BasicDijkstra<CsrOut, StdHeap<HeapItem, HeapItemLess>>;

// ---------------------------------------------------------------- shared helpers

Dist path_cost(const Graph& g, const std::vector<NodeId>& path) {
    Dist total = 0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const Dist w = g.edge_weight(path[i - 1], path[i]);
        if (w == kInfDist) return kInfDist;
        total = sat_add(total, static_cast<Weight>(w));
    }
    return total;
}

std::uint64_t path_length_mm(const Graph& g, const std::vector<NodeId>& path) {
    std::uint64_t total = 0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const std::uint32_t e = g.find_edge(path[i - 1], path[i]);
        if (e == UINT32_MAX) throw std::invalid_argument("path_length_mm: not a path");
        total += g.lengths_mm[e];
    }
    return total;
}

std::string_view algorithm_name(Algorithm a) {
    switch (a) {
        case Algorithm::Dijkstra: return "dijkstra";
        case Algorithm::Bidirectional: return "bidirectional";
        case Algorithm::AStar: return "astar";
    }
    return "?";
}

Algorithm parse_algorithm(std::string_view s) {
    if (s == "dijkstra") return Algorithm::Dijkstra;
    if (s == "bidirectional" || s == "bidir") return Algorithm::Bidirectional;
    if (s == "astar" || s == "a*") return Algorithm::AStar;
    throw std::invalid_argument("unknown routing algorithm: " + std::string(s));
}

Router::Router(const Graph& g, Algorithm a)
    : g_(g), algo_(a), dij_(make_dijkstra(g)), bi_(g), astar_(g) {}

RouteResult Router::route(NodeId s, NodeId t, bool with_path) {
    switch (algo_) {
        case Algorithm::Dijkstra: return dij_.query(s, t, with_path);
        case Algorithm::Bidirectional: return bi_.query(s, t, with_path);
        case Algorithm::AStar: return astar_.query(s, t, with_path);
    }
    throw std::logic_error("unreachable");
}

}  // namespace dispatchlab
