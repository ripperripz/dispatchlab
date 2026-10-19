#include "dispatchlab/cch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include "dispatchlab/counters.hpp"

namespace dispatchlab {

namespace {

constexpr std::uint32_t kNoMid = UINT32_MAX;

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Undirected, deduplicated neighbour lists (CSR) of the directed graph.
void undirected(const Graph& g, std::vector<std::uint32_t>& off, std::vector<std::uint32_t>& adj) {
    const std::uint32_t n = g.num_nodes();
    off.assign(static_cast<std::size_t>(n) + 1, 0);
    adj.clear();
    std::vector<std::uint32_t> tmp;
    for (NodeId u = 0; u < n; ++u) {
        tmp.clear();
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) tmp.push_back(g.targets[e]);
        for (auto e = g.in_begin(u); e < g.in_end(u); ++e) tmp.push_back(g.rev_sources[e]);
        std::sort(tmp.begin(), tmp.end());
        tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());
        adj.insert(adj.end(), tmp.begin(), tmp.end());
        off[u + 1] = static_cast<std::uint32_t>(adj.size());
    }
}

// Recursive geometric nested dissection with an explicit work stack. Each cell
// is split at the median of its wider coordinate axis; the smaller of the two
// boundary sets is the separator and receives the highest remaining ranks, so
// separators always rank above both halves.
std::vector<std::uint32_t> nested_dissection_order(const Graph& g, const std::vector<std::uint32_t>& off,
                                                   const std::vector<std::uint32_t>& adj) {
    const std::uint32_t n = g.num_nodes();
    std::vector<std::uint32_t> rank(n, UINT32_MAX);
    std::vector<std::uint32_t> side(n, 0);  // per-cell membership: 0 none, 1 A, 2 B
    std::int64_t next = static_cast<std::int64_t>(n) - 1;
    std::vector<std::vector<NodeId>> stack;
    {
        std::vector<NodeId> all(n);
        for (NodeId v = 0; v < n; ++v) all[v] = v;
        stack.push_back(std::move(all));
    }
    while (!stack.empty()) {
        std::vector<NodeId> cell = std::move(stack.back());
        stack.pop_back();
        if (cell.empty()) continue;
        if (cell.size() <= 2) {
            for (NodeId v : cell) rank[v] = static_cast<std::uint32_t>(next--);
            continue;
        }
        double min_lat = 1e9, max_lat = -1e9, min_lon = 1e9, max_lon = -1e9;
        for (NodeId v : cell) {
            min_lat = std::min(min_lat, g.lat[v]);
            max_lat = std::max(max_lat, g.lat[v]);
            min_lon = std::min(min_lon, g.lon[v]);
            max_lon = std::max(max_lon, g.lon[v]);
        }
        const double k = std::cos((min_lat + max_lat) * 0.5 * 3.14159265358979323846 / 180.0);
        const bool by_lon = (max_lon - min_lon) * k >= (max_lat - min_lat);
        const auto key = [&](NodeId v) { return by_lon ? g.lon[v] : g.lat[v]; };
        const std::size_t half = cell.size() / 2;
        std::nth_element(cell.begin(), cell.begin() + static_cast<std::ptrdiff_t>(half), cell.end(),
                         [&](NodeId a, NodeId b) { return key(a) != key(b) ? key(a) < key(b) : a < b; });
        for (std::size_t i = 0; i < cell.size(); ++i) side[cell[i]] = i < half ? 1 : 2;
        std::vector<NodeId> sep_a, sep_b;
        for (NodeId v : cell) {
            const std::uint32_t mine = side[v];
            for (auto e = off[v]; e < off[v + 1]; ++e) {
                const std::uint32_t other = side[adj[e]];
                if (other != 0 && other != mine) {
                    (mine == 1 ? sep_a : sep_b).push_back(v);
                    break;
                }
            }
        }
        std::vector<NodeId> sep = sep_a.size() <= sep_b.size() ? std::move(sep_a) : std::move(sep_b);
        std::sort(sep.begin(), sep.end());
        for (NodeId v : sep) {
            rank[v] = static_cast<std::uint32_t>(next--);
            side[v] = 3;  // removed
        }
        std::vector<NodeId> a, b;
        for (NodeId v : cell) {
            if (side[v] == 1) a.push_back(v);
            else if (side[v] == 2) b.push_back(v);
        }
        for (NodeId v : cell) side[v] = 0;
        // Both halves are strictly smaller than the cell, so this terminates.
        stack.push_back(std::move(a));
        stack.push_back(std::move(b));
    }
    if (next != -1) throw std::logic_error("nested dissection did not rank every node");
    return rank;
}

}  // namespace

CCH::CCH(const Graph& g) : n_(g.num_nodes()), fwd_(g.num_nodes()), bwd_(g.num_nodes()) {
    std::vector<std::uint32_t> off, adj;
    undirected(g, off, adj);
    stats_.original_pairs = adj.size() / 2;

    auto t0 = std::chrono::steady_clock::now();
    rank_ = nested_dissection_order(g, off, adj);
    order_.assign(n_, 0);
    for (NodeId v = 0; v < n_; ++v) order_[rank_[v]] = v;
    stats_.order_ms = ms_since(t0);

    // Chordal fill-in: eliminating node r makes its upward neighbours a clique;
    // propagating them to the lowest upward neighbour (elimination-tree parent)
    // is sufficient.
    t0 = std::chrono::steady_clock::now();
    std::vector<std::vector<std::uint32_t>> up(n_);
    for (NodeId v = 0; v < n_; ++v) {
        for (auto e = off[v]; e < off[v + 1]; ++e) {
            const std::uint32_t rv = rank_[v], ru = rank_[adj[e]];
            if (ru > rv) up[rv].push_back(ru);
        }
    }
    for (std::uint32_t r = 0; r < n_; ++r) {
        auto& u = up[r];
        std::sort(u.begin(), u.end());
        u.erase(std::unique(u.begin(), u.end()), u.end());
        if (u.size() > 1) {
            auto& parent = up[u[0]];
            parent.insert(parent.end(), u.begin() + 1, u.end());
        }
    }
    up_offsets_.assign(static_cast<std::size_t>(n_) + 1, 0);
    for (std::uint32_t r = 0; r < n_; ++r) {
        up_offsets_[r + 1] = up_offsets_[r] + static_cast<std::uint32_t>(up[r].size());
        stats_.max_up_degree = std::max(stats_.max_up_degree, static_cast<std::uint32_t>(up[r].size()));
    }
    up_targets_.reserve(up_offsets_.back());
    for (auto& u : up) {
        up_targets_.insert(up_targets_.end(), u.begin(), u.end());
        std::vector<std::uint32_t>().swap(u);
    }
    stats_.upward_edges = up_targets_.size();
    stats_.contract_ms = ms_since(t0);

    customize(g);
}

std::uint32_t CCH::find_edge(std::uint32_t lo, std::uint32_t hi) const {
    const auto first = up_targets_.begin() + up_offsets_[lo];
    const auto last = up_targets_.begin() + up_offsets_[lo + 1];
    const auto it = std::lower_bound(first, last, hi);
    if (it == last || *it != hi) throw std::logic_error("CCH: missing upward edge");
    return static_cast<std::uint32_t>(it - up_targets_.begin());
}

void CCH::customize(const Graph& g) {
    if (g.num_nodes() != n_) throw std::invalid_argument("CCH::customize: graph size differs");
    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t m = up_targets_.size();
    up_w_.assign(m, kInfDist);
    down_w_.assign(m, kInfDist);
    up_mid_.assign(m, kNoMid);
    down_mid_.assign(m, kNoMid);
    // Original edges.
    for (NodeId u = 0; u < n_; ++u) {
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) {
            const std::uint32_t ru = rank_[u], rv = rank_[g.targets[e]];
            const Dist w = g.weights[e];
            if (ru < rv) {
                const std::uint32_t id = find_edge(ru, rv);
                up_w_[id] = std::min(up_w_[id], w);
            } else {
                const std::uint32_t id = find_edge(rv, ru);
                down_w_[id] = std::min(down_w_[id], w);
            }
        }
    }
    // Lower triangles x < u < v, processed with x ascending, so (x,u) and (x,v)
    // are final when used:
    //   u->v  <=  u->x + x->v   =  down(x,u) + up(x,v)
    //   v->u  <=  v->x + x->u   =  down(x,v) + up(x,u)
    std::uint64_t triangles = 0;
    for (std::uint32_t x = 0; x < n_; ++x) {
        const std::uint32_t xb = up_offsets_[x], xe = up_offsets_[x + 1];
        for (std::uint32_t i = xb; i < xe; ++i) {
            const std::uint32_t u = up_targets_[i];
            std::uint32_t p = up_offsets_[u];  // merge pointer into U(u)
            const std::uint32_t pe = up_offsets_[u + 1];
            for (std::uint32_t j = i + 1; j < xe; ++j) {
                const std::uint32_t v = up_targets_[j];
                while (p < pe && up_targets_[p] < v) ++p;
                if (p == pe || up_targets_[p] != v) throw std::logic_error("CCH: topology not chordal");
                ++triangles;
                const Dist a = down_w_[i], b = up_w_[j];
                if (a != kInfDist && b != kInfDist && a + b < up_w_[p]) {
                    up_w_[p] = a + b;
                    up_mid_[p] = x;
                }
                const Dist c = down_w_[j], d = up_w_[i];
                if (c != kInfDist && d != kInfDist && c + d < down_w_[p]) {
                    down_w_[p] = c + d;
                    down_mid_[p] = x;
                }
            }
        }
    }
    stats_.triangles = triangles;
    stats_.customize_ms = ms_since(t0);
}

RouteResult CCH::query(NodeId s, NodeId t, bool with_path, std::vector<NodeId>* trace) {
    if (s >= n_ || t >= n_) throw std::out_of_range("CCH: node id out of range");
    count_routing_call();
    RouteResult r;
    fwd_.reset();
    bwd_.reset();
    fheap_.clear();
    bheap_.clear();
    const std::uint32_t rs = rank_[s], rt = rank_[t];
    fwd_.set(rs, 0, kInvalidNode);
    bwd_.set(rt, 0, kInvalidNode);
    fheap_.push({0, rs});
    bheap_.push({0, rt});
    Dist mu = kInfDist;
    std::uint32_t meet = kInvalidNode;
    // Both searches only go upward; each stops once its smallest key >= mu.
    while (!fheap_.empty() || !bheap_.empty()) {
        const bool fwd_ok = !fheap_.empty() && fheap_.top().key < mu;
        const bool bwd_ok = !bheap_.empty() && bheap_.top().key < mu;
        if (!fwd_ok && !bwd_ok) break;
        const bool forward = fwd_ok && (!bwd_ok || fheap_.top().key <= bheap_.top().key);
        SearchWorkspace& ws = forward ? fwd_ : bwd_;
        SearchWorkspace& other = forward ? bwd_ : fwd_;
        RoutingHeap& heap = forward ? fheap_ : bheap_;
        const HeapItem top = heap.take();
        if (top.key != ws.dist(top.node)) continue;
        ++r.stats.settled;
        const std::uint32_t x = top.node;
        if (trace) trace->push_back(order_[x]);
        if (other.dist(x) != kInfDist && top.key + other.dist(x) < mu) {
            mu = top.key + other.dist(x);
            meet = x;
        }
        const std::vector<Dist>& w = forward ? up_w_ : down_w_;
        for (std::uint32_t e = up_offsets_[x]; e < up_offsets_[x + 1]; ++e) {
            if (w[e] == kInfDist) continue;
            const std::uint32_t y = up_targets_[e];
            const Dist nd = top.key + w[e];
            if (nd < ws.dist(y)) {
                ws.set(y, nd, x);
                heap.push({nd, y});
                ++r.stats.relaxed;
            }
        }
    }
    if (mu == kInfDist) return r;
    r.cost = mu;
    if (with_path) {
        std::vector<std::uint32_t> up_chain;  // s .. meet (ranks)
        for (std::uint32_t v = meet; v != kInvalidNode; v = fwd_.parent(v)) up_chain.push_back(v);
        std::reverse(up_chain.begin(), up_chain.end());
        for (std::uint32_t v = bwd_.parent(meet); v != kInvalidNode; v = bwd_.parent(v)) up_chain.push_back(v);
        r.path.push_back(order_[up_chain.front()]);
        for (std::size_t i = 1; i < up_chain.size(); ++i) unpack(up_chain[i - 1], up_chain[i], r.path);
    }
    return r;
}

// Appends the original nodes of the hierarchy edge from->to (excluding `from`).
void CCH::unpack(std::uint32_t from, std::uint32_t to, std::vector<NodeId>& out) const {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> stack{{from, to}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        const std::uint32_t lo = std::min(a, b), hi = std::max(a, b);
        const std::uint32_t e = find_edge(lo, hi);
        const std::uint32_t mid = a == lo ? up_mid_[e] : down_mid_[e];
        if (mid == kNoMid) {
            out.push_back(order_[b]);
        } else {
            stack.push_back({mid, b});  // processed second
            stack.push_back({a, mid});  // processed first
        }
    }
}

std::size_t CCH::memory_bytes() const {
    return (rank_.capacity() + order_.capacity() + up_offsets_.capacity() + up_targets_.capacity() +
            up_mid_.capacity() + down_mid_.capacity()) * sizeof(std::uint32_t) +
           (up_w_.capacity() + down_w_.capacity()) * sizeof(Dist);
}

std::size_t CCH::query_memory_bytes() const {
    return fwd_.memory_bytes() + bwd_.memory_bytes() + (fheap_.capacity() + bheap_.capacity()) * sizeof(HeapItem);
}

}  // namespace dispatchlab
