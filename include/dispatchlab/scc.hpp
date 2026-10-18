#pragma once

#include <cstdint>
#include <vector>

#include "dispatchlab/graph.hpp"

namespace dispatchlab {

struct SccResult {
    std::vector<std::uint32_t> component;  // component id per node
    std::vector<std::uint32_t> sizes;      // size per component id
    std::uint32_t num_components() const { return static_cast<std::uint32_t>(sizes.size()); }
    // Id of the largest component (ties: lowest id).
    std::uint32_t largest() const;
};

// Tarjan's algorithm with an explicit stack (no recursion), so it is safe on
// graphs with very long paths. O(V + E) time and memory. Component ids are
// assigned in the order components are completed (deterministic).
SccResult strongly_connected_components(std::uint32_t num_nodes,
                                        const std::vector<std::uint32_t>& offsets,
                                        const std::vector<std::uint32_t>& targets);
SccResult strongly_connected_components(const Graph& g);

// Induced subgraph on the largest SCC.
Graph largest_scc_subgraph(const Graph& g);

}  // namespace dispatchlab
