#pragma once

// Customizable Contraction Hierarchies (PRD FR-14, P2 stretch goal), basic
// variant only:
//   1. metric-independent node order: recursive geometric nested dissection
//      (iterative; separators get the highest ranks),
//   2. contraction topology: chordal fill-in of the undirected graph under
//      that order (elimination-tree propagation),
//   3. basic customization by lower-triangle enumeration in rank order,
//   4. queries: pruned bidirectional Dijkstra on the upward graph,
//   5. path unpacking via the middle node recorded for each shortcut.

#include <cstdint>
#include <memory>
#include <vector>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/routing.hpp"

namespace dispatchlab {

struct CchStats {
    double order_ms = 0, contract_ms = 0, customize_ms = 0;
    std::uint64_t upward_edges = 0;    // edges of the chordal supergraph
    std::uint64_t original_pairs = 0;  // undirected node pairs in the input graph
    std::uint64_t triangles = 0;       // lower triangles relaxed during customization
    std::uint32_t max_up_degree = 0;
};

// The metric-independent nested-dissection rank of every node (separators get
// the highest ranks); also used by the graph-layout study.
std::vector<std::uint32_t> nested_dissection_ranks(const Graph& g);

// The hierarchy is split into an immutable, shared topology and metric, and
// per-instance query workspaces. Copying a CCH is cheap (it shares the
// hierarchy), so each thread can own a copy and query concurrently.
class CCH {
public:
    struct Topology {
        std::vector<std::uint32_t> rank;   // node -> rank
        std::vector<std::uint32_t> order;  // rank -> node
        std::vector<std::uint32_t> up_offsets, up_targets;  // upward CSR in rank space, targets ascending
    };
    struct Metric {
        std::vector<Dist> up_w, down_w;  // per upward edge (lo,hi): weight lo->hi and hi->lo
        std::vector<std::uint32_t> up_mid, down_mid;  // middle rank of a shortcut, or kNoMid
    };

    // Order + topology (metric independent) and an initial customization with g's weights.
    explicit CCH(const Graph& g);

    // Re-runs basic customization for new weights on the same topology
    // (e.g. a traffic scenario of the same graph). Replaces this instance's
    // metric; copies made earlier keep the old one.
    void customize(const Graph& g);

    // `trace` receives settled nodes (original ids) in settle order.
    RouteResult query(NodeId s, NodeId t, bool with_path = true, std::vector<NodeId>* trace = nullptr);

    // One-to-many support (bucket-style): a complete backward upward search from
    // `t` whose labels are kept, then cheap pruned forward searches from each
    // source. Returns nodes settled. distance_from_prepared(s) == query(s, t).cost.
    std::uint32_t prepare_target(NodeId t);
    Dist distance_from_prepared(NodeId s, std::uint32_t* settled = nullptr);

    const CchStats& stats() const { return stats_; }
    std::size_t memory_bytes() const;  // hierarchy (topology + metric), excluding query workspaces
    std::size_t query_memory_bytes() const;
    std::uint32_t rank_of(NodeId v) const { return topo_->rank[v]; }

private:
    std::uint32_t find_edge(std::uint32_t lo, std::uint32_t hi) const;  // ranks, lo < hi
    void unpack(std::uint32_t from, std::uint32_t to, std::vector<NodeId>& out) const;

    std::uint32_t n_ = 0;
    std::shared_ptr<const Topology> topo_;
    std::shared_ptr<const Metric> metric_;
    CchStats stats_;

    SearchWorkspace fwd_, bwd_;
    RoutingHeap fheap_, bheap_;
};

}  // namespace dispatchlab
