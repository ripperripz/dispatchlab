#include <algorithm>
#include <stdexcept>

#include "dispatchlab/counters.hpp"
#include "dispatchlab/routing.hpp"

namespace dispatchlab {

BidirectionalDijkstra::BidirectionalDijkstra(const Graph& g)
    : g_(g), fwd_(g.num_nodes()), bwd_(g.num_nodes()) {}

std::size_t BidirectionalDijkstra::memory_bytes() const {
    return fwd_.memory_bytes() + bwd_.memory_bytes() +
           (fheap_.capacity() + bheap_.capacity()) * sizeof(HeapItem);
}

// Correctness of the stopping rule: let mu be the best s-t path cost seen so far
// through a scanned edge. Any s-t path P of cost < mu must contain an edge (x,y)
// with x not yet settled forward or y not yet settled backward, implying
// cost(P) >= top_f + top_b. Hence once top_f + top_b >= mu, mu is optimal.
// Stale heap entries only make the tops smaller (never larger than the true
// minimum label), so testing with them is conservative and still correct.
RouteResult BidirectionalDijkstra::query(NodeId s, NodeId t, bool with_path, std::vector<NodeId>* trace) {
    const std::uint32_t n = g_.num_nodes();
    if (s >= n || t >= n) throw std::out_of_range("Bidirectional: node id out of range");
    count_routing_call();
    RouteResult r;
    fwd_.reset();
    bwd_.reset();
    fheap_.clear();
    bheap_.clear();
    fwd_.set(s, 0, kInvalidNode);
    bwd_.set(t, 0, kInvalidNode);  // parent in bwd_ = next node towards t
    fheap_.push({0, s});
    bheap_.push({0, t});
    r.stats.pushes = 2;

    Dist mu = (s == t) ? 0 : kInfDist;
    NodeId meet = (s == t) ? s : kInvalidNode;

    while (!fheap_.empty() && !bheap_.empty()) {
        const Dist tf = fheap_.top().key;
        const Dist tb = bheap_.top().key;
        if (mu != kInfDist && tf + tb >= mu) break;

        if (tf <= tb) {
            const HeapItem top = fheap_.take();
            if (top.key != fwd_.dist(top.node)) continue;
            ++r.stats.settled;
            const NodeId u = top.node;
            if (trace) trace->push_back(u);
            const std::uint32_t end = g_.offsets[u + 1];
            for (std::uint32_t e = g_.offsets[u]; e < end; ++e) {
                const NodeId v = g_.targets[e];
                const Dist nd = sat_add(top.key, g_.weights[e]);
                if (nd < fwd_.dist(v)) {
                    fwd_.set(v, nd, u);
                    fheap_.push({nd, v});
                    ++r.stats.relaxed;
                    ++r.stats.pushes;
                }
                // Meeting candidate through v using v's current labels (fwd label <= nd),
                // so the recorded cost always corresponds to the parent chains of v.
                const Dist db = bwd_.dist(v);
                if (db != kInfDist && fwd_.dist(v) + db < mu) {
                    mu = fwd_.dist(v) + db;
                    meet = v;
                }
            }
        } else {
            const HeapItem top = bheap_.take();
            if (top.key != bwd_.dist(top.node)) continue;
            ++r.stats.settled;
            const NodeId v = top.node;
            if (trace) trace->push_back(v);
            const std::uint32_t end = g_.rev_offsets[v + 1];
            for (std::uint32_t e = g_.rev_offsets[v]; e < end; ++e) {
                const NodeId u = g_.rev_sources[e];
                const Dist nd = sat_add(top.key, g_.rev_weights[e]);
                if (nd < bwd_.dist(u)) {
                    bwd_.set(u, nd, v);
                    bheap_.push({nd, u});
                    ++r.stats.relaxed;
                    ++r.stats.pushes;
                }
                const Dist df = fwd_.dist(u);
                if (df != kInfDist && df + bwd_.dist(u) < mu) {
                    mu = df + bwd_.dist(u);
                    meet = u;
                }
            }
        }
    }

    if (mu == kInfDist) return r;
    r.cost = mu;
    if (with_path) {
        std::vector<NodeId> path;
        for (NodeId v = meet; v != kInvalidNode; v = fwd_.parent(v)) path.push_back(v);
        std::reverse(path.begin(), path.end());
        for (NodeId v = bwd_.parent(meet); v != kInvalidNode; v = bwd_.parent(v)) path.push_back(v);
        r.path = std::move(path);
    }
    return r;
}

}  // namespace dispatchlab
