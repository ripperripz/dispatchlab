#include "dispatchlab/scc.hpp"

#include <algorithm>
#include <limits>

namespace dispatchlab {

std::uint32_t SccResult::largest() const {
    std::uint32_t best = 0;
    for (std::uint32_t c = 1; c < sizes.size(); ++c) {
        if (sizes[c] > sizes[best]) best = c;
    }
    return best;
}

SccResult strongly_connected_components(std::uint32_t n, const std::vector<std::uint32_t>& offsets,
                                        const std::vector<std::uint32_t>& targets) {
    constexpr std::uint32_t kUnvisited = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> index(n, kUnvisited);
    std::vector<std::uint32_t> low(n, 0);
    std::vector<char> on_stack(n, 0);
    std::vector<NodeId> stack;  // Tarjan's node stack

    // Explicit DFS call stack: node and the next out-edge position to examine.
    struct Frame {
        NodeId node;
        std::uint32_t next_edge;
    };
    std::vector<Frame> frames;

    SccResult result;
    result.component.assign(n, 0);
    std::uint32_t next_index = 0;

    for (NodeId root = 0; root < n; ++root) {
        if (index[root] != kUnvisited) continue;
        index[root] = low[root] = next_index++;
        stack.push_back(root);
        on_stack[root] = 1;
        frames.push_back({root, offsets[root]});

        while (!frames.empty()) {
            Frame& f = frames.back();
            const NodeId u = f.node;
            if (f.next_edge < offsets[u + 1]) {
                const NodeId v = targets[f.next_edge++];
                if (index[v] == kUnvisited) {
                    index[v] = low[v] = next_index++;
                    stack.push_back(v);
                    on_stack[v] = 1;
                    frames.push_back({v, offsets[v]});  // "recurse"; f may dangle now
                } else if (on_stack[v]) {
                    low[u] = std::min(low[u], index[v]);
                }
                continue;
            }
            // All edges of u done: u is a root iff low == index.
            if (low[u] == index[u]) {
                const std::uint32_t cid = static_cast<std::uint32_t>(result.sizes.size());
                std::uint32_t size = 0;
                NodeId w;
                do {
                    w = stack.back();
                    stack.pop_back();
                    on_stack[w] = 0;
                    result.component[w] = cid;
                    ++size;
                } while (w != u);
                result.sizes.push_back(size);
            }
            frames.pop_back();
            if (!frames.empty()) {
                const NodeId parent = frames.back().node;
                low[parent] = std::min(low[parent], low[u]);
            }
        }
    }
    return result;
}

SccResult strongly_connected_components(const Graph& g) {
    return strongly_connected_components(g.num_nodes(), g.offsets, g.targets);
}

Graph largest_scc_subgraph(const Graph& g) {
    if (g.num_nodes() == 0) return g;
    const SccResult scc = strongly_connected_components(g);
    const std::uint32_t big = scc.largest();
    std::vector<char> keep(g.num_nodes(), 0);
    for (NodeId v = 0; v < g.num_nodes(); ++v) keep[v] = scc.component[v] == big;
    return induced_subgraph(g, keep);
}

}  // namespace dispatchlab
