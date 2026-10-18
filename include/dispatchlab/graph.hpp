#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dispatchlab/types.hpp"

namespace dispatchlab {

// Input edge used to build a CSR graph.
struct EdgeInput {
    NodeId from;
    NodeId to;
    Weight weight;            // travel time, ms (> 0)
    std::uint32_t length_mm;  // geometric length, millimetres
    std::uint8_t road_class;  // RoadClass, or 0 for synthetic fixtures
};

struct GraphMeta {
    // Upper bound on any speed used to derive edge weights (km/h). A* uses it as
    // v_max; build_csr never checks it, validate() does.
    std::uint32_t max_speed_kmh = 0;
    std::string source;  // dataset provenance (file, date, bbox)
};

// Directed graph in Compressed Sparse Row form, plus a reverse CSR for
// backward searches. Out-edges of u are [offsets[u], offsets[u+1]).
// Reverse edges into v are [rev_offsets[v], rev_offsets[v+1]); rev_sources
// holds the tail node and rev_weights the same weight as the forward edge.
struct Graph {
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> targets;
    std::vector<std::uint32_t> weights;

    std::vector<std::uint32_t> lengths_mm;  // parallel to targets
    std::vector<std::uint8_t> road_class;   // parallel to targets

    std::vector<std::uint32_t> rev_offsets;
    std::vector<std::uint32_t> rev_sources;
    std::vector<std::uint32_t> rev_weights;
    std::vector<std::uint32_t> rev_edge;  // forward edge id of each reverse entry

    std::vector<double> lat;               // degrees, per node
    std::vector<double> lon;               // degrees, per node
    std::vector<std::uint64_t> osm_ids;    // original OSM node id per compact id (0 for fixtures)

    GraphMeta meta;

    std::uint32_t num_nodes() const {
        return offsets.empty() ? 0 : static_cast<std::uint32_t>(offsets.size() - 1);
    }
    std::uint32_t num_edges() const { return static_cast<std::uint32_t>(targets.size()); }
    std::uint32_t out_begin(NodeId u) const { return offsets[u]; }
    std::uint32_t out_end(NodeId u) const { return offsets[u + 1]; }
    std::uint32_t in_begin(NodeId v) const { return rev_offsets[v]; }
    std::uint32_t in_end(NodeId v) const { return rev_offsets[v + 1]; }

    // Weight of edge u->v, or kInfDist if absent (binary search; targets of a
    // node are sorted ascending and unique).
    Dist edge_weight(NodeId u, NodeId v) const;
    // Edge id of u->v or UINT32_MAX.
    std::uint32_t find_edge(NodeId u, NodeId v) const;

    // Bytes held by the forward + reverse CSR arrays and per-node data.
    std::size_t memory_bytes() const;
    std::size_t csr_core_bytes() const;  // offsets+targets+weights only
};

// Builds a CSR graph. Self-loops are dropped; parallel edges u->v are merged
// keeping the minimum weight (ties: shorter length, then lower class). The
// result is independent of input edge order. Coordinates/ids may be empty, in
// which case they are zero-filled. The reverse CSR is built as well.
Graph build_csr(std::uint32_t num_nodes, std::vector<EdgeInput> edges,
                std::vector<double> lat = {}, std::vector<double> lon = {},
                std::vector<std::uint64_t> osm_ids = {});

// (Re)builds the reverse CSR from the forward CSR. Within each target bucket,
// sources are sorted ascending.
void build_reverse(Graph& g);

// Induced subgraph on nodes with keep[v] != 0. New ids preserve the relative
// order of old ids.
Graph induced_subgraph(const Graph& g, const std::vector<char>& keep);

// Returns an empty string if the graph is structurally valid, else a
// description of the first problem found: offsets monotone and consistent,
// targets in range, weights > 0, targets sorted+unique per node, no self loops,
// reverse CSR consistent with forward CSR, parallel arrays sized correctly,
// and (if max_speed_kmh > 0) every edge's implied speed <= max_speed_kmh.
std::string validate(const Graph& g);

// Independent strong-connectivity check via forward and backward BFS from node 0.
bool is_strongly_connected(const Graph& g);

// Binary serialization ("DLGRAPH1" format, see docs/DATA.md). The file ends with
// an FNV-1a 64 checksum of all preceding bytes; load() verifies it and rebuilds
// the reverse CSR.
void save_graph(const Graph& g, const std::string& path);
Graph load_graph(const std::string& path);

bool graphs_equal(const Graph& a, const Graph& b);

// Adjacency-list baseline (vector of vectors) for the CSR comparison.
struct AdjEdge {
    NodeId target;
    Weight weight;
};
struct AdjacencyGraph {
    std::vector<std::vector<AdjEdge>> out;
    std::uint32_t num_nodes() const { return static_cast<std::uint32_t>(out.size()); }
    std::size_t memory_bytes() const;
};
AdjacencyGraph to_adjacency(const Graph& g);
// Loads the same binary file directly into an adjacency list (for the load-time comparison).
AdjacencyGraph load_adjacency(const std::string& path);

}  // namespace dispatchlab
