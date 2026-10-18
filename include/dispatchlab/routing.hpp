#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dispatchlab/binary_heap.hpp"
#include "dispatchlab/graph.hpp"
#include "dispatchlab/types.hpp"

namespace dispatchlab {

struct QueryStats {
    std::uint32_t settled = 0;  // non-stale heap pops (nodes scanned)
    std::uint32_t relaxed = 0;  // successful edge relaxations
    std::uint32_t pushes = 0;   // heap insertions
};

struct RouteResult {
    Dist cost = kInfDist;       // kInfDist => unreachable
    std::vector<NodeId> path;   // s..t inclusive (empty if unreachable or not requested)
    QueryStats stats;
    bool reachable() const { return cost != kInfDist; }
};

// Heap entry; ties on key are broken by node id so pop order (and therefore the
// chosen shortest path among equal-cost ones) does not depend on heap internals.
struct HeapItem {
    Dist key;
    NodeId node;
};
struct HeapItemLess {
    bool operator()(const HeapItem& a, const HeapItem& b) const {
        return a.key < b.key || (a.key == b.key && a.node < b.node);
    }
};
using RoutingHeap = BinaryHeap<HeapItem, HeapItemLess>;

// Per-query labels with O(1) reset: a label is valid only if stamp == epoch.
class SearchWorkspace {
public:
    explicit SearchWorkspace(std::uint32_t n = 0) { resize(n); }
    void resize(std::uint32_t n) {
        dist_.assign(n, kInfDist);
        parent_.assign(n, kInvalidNode);
        stamp_.assign(n, 0);
        epoch_ = 0;
    }
    void reset() {
        if (++epoch_ == 0) {  // wrapped: clear stamps once every 2^32 queries
            std::fill(stamp_.begin(), stamp_.end(), 0);
            epoch_ = 1;
        }
    }
    Dist dist(NodeId v) const { return stamp_[v] == epoch_ ? dist_[v] : kInfDist; }
    NodeId parent(NodeId v) const { return stamp_[v] == epoch_ ? parent_[v] : kInvalidNode; }
    void set(NodeId v, Dist d, NodeId p) {
        stamp_[v] = epoch_;
        dist_[v] = d;
        parent_[v] = p;
    }
    std::size_t memory_bytes() const {
        return dist_.capacity() * sizeof(Dist) + parent_.capacity() * sizeof(NodeId) +
               stamp_.capacity() * sizeof(std::uint32_t);
    }

private:
    std::vector<Dist> dist_;
    std::vector<NodeId> parent_;
    std::vector<std::uint32_t> stamp_;
    std::uint32_t epoch_ = 0;
};

// Graph accessors so the same Dijkstra code runs on forward CSR, reverse CSR and
// the adjacency-list baseline.
struct CsrOut {
    const Graph* g;
    std::uint32_t num_nodes() const { return g->num_nodes(); }
    template <class F>
    void for_each(NodeId u, F&& f) const {
        const std::uint32_t end = g->offsets[u + 1];
        for (std::uint32_t e = g->offsets[u]; e < end; ++e) f(g->targets[e], g->weights[e]);
    }
};
struct CsrIn {
    const Graph* g;
    std::uint32_t num_nodes() const { return g->num_nodes(); }
    template <class F>
    void for_each(NodeId v, F&& f) const {
        const std::uint32_t end = g->rev_offsets[v + 1];
        for (std::uint32_t e = g->rev_offsets[v]; e < end; ++e) f(g->rev_sources[e], g->rev_weights[e]);
    }
};
struct AdjOut {
    const AdjacencyGraph* g;
    std::uint32_t num_nodes() const { return g->num_nodes(); }
    template <class F>
    void for_each(NodeId u, F&& f) const {
        for (const AdjEdge& e : g->out[u]) f(e.target, e.weight);
    }
};

// Reference exact shortest paths (Dijkstra, binary heap, lazy deletion).
// O((V + E) log V) per query.
template <class Access>
class BasicDijkstra {
public:
    explicit BasicDijkstra(Access access);
    RouteResult query(NodeId s, NodeId t, bool with_path = true);
    // Distances from s to each target (kInfDist if unreachable). Stops as soon as
    // every target is settled. With CsrIn this yields distances *to* s.
    std::vector<Dist> one_to_many(NodeId s, const std::vector<NodeId>& targets,
                                  QueryStats* stats = nullptr);
    // Full single-source search; afterwards dist(v) is exact for all v.
    void one_to_all(NodeId s, QueryStats* stats = nullptr);
    Dist dist(NodeId v) const { return ws_.dist(v); }
    std::size_t memory_bytes() const { return ws_.memory_bytes() + heap_.capacity() * sizeof(HeapItem); }

private:
    Access access_;
    SearchWorkspace ws_;
    RoutingHeap heap_;
    std::vector<std::uint32_t> target_mark_;  // for one_to_many
    std::uint32_t mark_epoch_ = 0;
};

using Dijkstra = BasicDijkstra<CsrOut>;
using ReverseDijkstra = BasicDijkstra<CsrIn>;
using AdjacencyDijkstra = BasicDijkstra<AdjOut>;

inline Dijkstra make_dijkstra(const Graph& g) { return Dijkstra(CsrOut{&g}); }
inline ReverseDijkstra make_reverse_dijkstra(const Graph& g) { return ReverseDijkstra(CsrIn{&g}); }

// Bidirectional Dijkstra: forward search on G, backward search on the reverse
// CSR. Stops when top_f + top_b >= mu (best meeting cost found so far).
class BidirectionalDijkstra {
public:
    explicit BidirectionalDijkstra(const Graph& g);
    RouteResult query(NodeId s, NodeId t, bool with_path = true);
    std::size_t memory_bytes() const;

private:
    const Graph& g_;
    SearchWorkspace fwd_, bwd_;
    RoutingHeap fheap_, bheap_;
};

// A* with the geodesic lower bound h(n) = floor(haversine(n, t) / v_max), where
// v_max = graph.meta.max_speed_kmh >= every speed used to compute weights.
// Because weights are rounded up and h is rounded down, h is admissible (and
// consistent up to floating-point rounding). Nodes may be re-expanded if a
// cheaper g is found, so the result is exact even if consistency is violated
// by rounding.
class AStar {
public:
    explicit AStar(const Graph& g);
    RouteResult query(NodeId s, NodeId t, bool with_path = true);
    Dist heuristic(NodeId v, NodeId t) const;
    std::uint32_t vmax_kmh() const { return vmax_kmh_; }
    std::size_t memory_bytes() const { return ws_.memory_bytes() + heap_.capacity() * sizeof(HeapItem); }

private:
    const Graph& g_;
    std::uint32_t vmax_kmh_;
    SearchWorkspace ws_;
    RoutingHeap heap_;
};

enum class Algorithm { Dijkstra, Bidirectional, AStar };
std::string_view algorithm_name(Algorithm a);
Algorithm parse_algorithm(std::string_view s);

// Owns one instance of each algorithm and dispatches to the selected one.
class Router {
public:
    Router(const Graph& g, Algorithm a);
    RouteResult route(NodeId s, NodeId t, bool with_path = true);
    Algorithm algorithm() const { return algo_; }
    const Graph& graph() const { return g_; }

private:
    const Graph& g_;
    Algorithm algo_;
    Dijkstra dij_;
    BidirectionalDijkstra bi_;
    AStar astar_;
};

// Sum of edge weights along `path`; kInfDist if some consecutive pair is not an edge.
Dist path_cost(const Graph& g, const std::vector<NodeId>& path);
// Sum of edge lengths (mm) along a valid path.
std::uint64_t path_length_mm(const Graph& g, const std::vector<NodeId>& path);

}  // namespace dispatchlab
