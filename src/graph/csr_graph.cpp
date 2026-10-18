#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/hash.hpp"

static_assert(std::endian::native == std::endian::little,
              "graph serialization assumes a little-endian host");

namespace dispatchlab {

namespace {

constexpr char kMagic[8] = {'D', 'L', 'G', 'R', 'A', 'P', 'H', '1'};
constexpr std::uint32_t kVersion = 1;

template <class T>
std::size_t vec_bytes(const std::vector<T>& v) {
    return v.capacity() * sizeof(T);
}

}  // namespace

Dist Graph::edge_weight(NodeId u, NodeId v) const {
    const std::uint32_t e = find_edge(u, v);
    return e == UINT32_MAX ? kInfDist : weights[e];
}

std::uint32_t Graph::find_edge(NodeId u, NodeId v) const {
    if (u >= num_nodes()) return UINT32_MAX;
    const auto first = targets.begin() + offsets[u];
    const auto last = targets.begin() + offsets[u + 1];
    const auto it = std::lower_bound(first, last, v);
    if (it == last || *it != v) return UINT32_MAX;
    return static_cast<std::uint32_t>(it - targets.begin());
}

std::size_t Graph::csr_core_bytes() const {
    return vec_bytes(offsets) + vec_bytes(targets) + vec_bytes(weights);
}

std::size_t Graph::memory_bytes() const {
    return csr_core_bytes() + vec_bytes(lengths_mm) + vec_bytes(road_class) +
           vec_bytes(rev_offsets) + vec_bytes(rev_sources) + vec_bytes(rev_weights) +
           vec_bytes(rev_edge) + vec_bytes(lat) + vec_bytes(lon) + vec_bytes(osm_ids);
}

Graph build_csr(std::uint32_t num_nodes, std::vector<EdgeInput> edges, std::vector<double> lat,
                std::vector<double> lon, std::vector<std::uint64_t> osm_ids) {
    for (const auto& e : edges) {
        if (e.from >= num_nodes || e.to >= num_nodes) {
            throw std::invalid_argument("build_csr: edge endpoint out of range");
        }
        if (e.weight == 0) throw std::invalid_argument("build_csr: zero edge weight");
    }
    // Total order over all fields => result independent of input order.
    std::sort(edges.begin(), edges.end(), [](const EdgeInput& a, const EdgeInput& b) {
        if (a.from != b.from) return a.from < b.from;
        if (a.to != b.to) return a.to < b.to;
        if (a.weight != b.weight) return a.weight < b.weight;
        if (a.length_mm != b.length_mm) return a.length_mm < b.length_mm;
        return a.road_class < b.road_class;
    });

    Graph g;
    g.offsets.assign(static_cast<std::size_t>(num_nodes) + 1, 0);
    g.targets.reserve(edges.size());
    g.weights.reserve(edges.size());
    g.lengths_mm.reserve(edges.size());
    g.road_class.reserve(edges.size());

    NodeId last_from = kInvalidNode;
    NodeId last_to = kInvalidNode;
    for (const auto& e : edges) {
        if (e.from == e.to) continue;                           // self-loop
        if (e.from == last_from && e.to == last_to) continue;  // parallel: first is minimal
        last_from = e.from;
        last_to = e.to;
        g.offsets[e.from + 1]++;
        g.targets.push_back(e.to);
        g.weights.push_back(e.weight);
        g.lengths_mm.push_back(e.length_mm);
        g.road_class.push_back(e.road_class);
    }
    for (std::uint32_t i = 0; i < num_nodes; ++i) g.offsets[i + 1] += g.offsets[i];

    if (lat.empty()) lat.assign(num_nodes, 0.0);
    if (lon.empty()) lon.assign(num_nodes, 0.0);
    if (osm_ids.empty()) osm_ids.assign(num_nodes, 0);
    if (lat.size() != num_nodes || lon.size() != num_nodes || osm_ids.size() != num_nodes) {
        throw std::invalid_argument("build_csr: per-node array size mismatch");
    }
    g.lat = std::move(lat);
    g.lon = std::move(lon);
    g.osm_ids = std::move(osm_ids);
    build_reverse(g);
    return g;
}

void build_reverse(Graph& g) {
    const std::uint32_t n = g.num_nodes();
    const std::uint32_t m = g.num_edges();
    g.rev_offsets.assign(static_cast<std::size_t>(n) + 1, 0);
    g.rev_sources.assign(m, 0);
    g.rev_weights.assign(m, 0);
    g.rev_edge.assign(m, 0);
    for (std::uint32_t e = 0; e < m; ++e) g.rev_offsets[g.targets[e] + 1]++;
    for (std::uint32_t i = 0; i < n; ++i) g.rev_offsets[i + 1] += g.rev_offsets[i];
    std::vector<std::uint32_t> pos(g.rev_offsets.begin(), g.rev_offsets.end() - 1);
    // Iterating tails in ascending order keeps each bucket sorted by source.
    for (NodeId u = 0; u < n; ++u) {
        for (std::uint32_t e = g.offsets[u]; e < g.offsets[u + 1]; ++e) {
            const std::uint32_t p = pos[g.targets[e]]++;
            g.rev_sources[p] = u;
            g.rev_weights[p] = g.weights[e];
            g.rev_edge[p] = e;
        }
    }
}

Graph induced_subgraph(const Graph& g, const std::vector<char>& keep) {
    const std::uint32_t n = g.num_nodes();
    if (keep.size() != n) throw std::invalid_argument("induced_subgraph: keep size mismatch");
    std::vector<NodeId> remap(n, kInvalidNode);
    std::uint32_t next = 0;
    std::vector<double> lat, lon;
    std::vector<std::uint64_t> ids;
    for (NodeId v = 0; v < n; ++v) {
        if (!keep[v]) continue;
        remap[v] = next++;
        lat.push_back(g.lat[v]);
        lon.push_back(g.lon[v]);
        ids.push_back(g.osm_ids[v]);
    }
    std::vector<EdgeInput> edges;
    for (NodeId u = 0; u < n; ++u) {
        if (!keep[u]) continue;
        for (std::uint32_t e = g.offsets[u]; e < g.offsets[u + 1]; ++e) {
            const NodeId v = g.targets[e];
            if (!keep[v]) continue;
            edges.push_back({remap[u], remap[v], g.weights[e], g.lengths_mm[e], g.road_class[e]});
        }
    }
    Graph out = build_csr(next, std::move(edges), std::move(lat), std::move(lon), std::move(ids));
    out.meta = g.meta;
    return out;
}

std::string validate(const Graph& g) {
    std::ostringstream err;
    const std::uint32_t n = g.num_nodes();
    const std::uint32_t m = g.num_edges();
    if (g.offsets.empty()) return "offsets empty";
    if (g.offsets.front() != 0) return "offsets[0] != 0";
    if (g.offsets.back() != m) return "offsets[n] != number of edges";
    if (g.weights.size() != m || g.lengths_mm.size() != m || g.road_class.size() != m) {
        return "edge-parallel array size mismatch";
    }
    if (g.lat.size() != n || g.lon.size() != n || g.osm_ids.size() != n) {
        return "node-parallel array size mismatch";
    }
    for (NodeId u = 0; u < n; ++u) {
        if (g.offsets[u] > g.offsets[u + 1]) {
            err << "offsets not monotone at " << u;
            return err.str();
        }
        for (std::uint32_t e = g.offsets[u]; e < g.offsets[u + 1]; ++e) {
            if (g.targets[e] >= n) {
                err << "edge " << e << " target out of range";
                return err.str();
            }
            if (g.targets[e] == u) {
                err << "self loop at " << u;
                return err.str();
            }
            if (e > g.offsets[u] && g.targets[e - 1] >= g.targets[e]) {
                err << "targets of node " << u << " not sorted/unique";
                return err.str();
            }
            if (g.weights[e] == 0) {
                err << "edge " << e << " has zero weight";
                return err.str();
            }
            if (g.meta.max_speed_kmh > 0) {
                // implied speed = length / time <= v_max  <=>  len_mm*3.6 <= vmax*w_ms.
                // lengths_mm is rounded to the nearest mm, so allow 0.5 mm of slack.
                const double lhs = (static_cast<double>(g.lengths_mm[e]) - 0.5) * 3.6;
                const double rhs = static_cast<double>(g.meta.max_speed_kmh) * g.weights[e];
                if (lhs > rhs) {
                    err << "edge " << e << " implies speed above max_speed_kmh";
                    return err.str();
                }
            }
        }
    }
    if (g.rev_offsets.size() != static_cast<std::size_t>(n) + 1 || g.rev_sources.size() != m ||
        g.rev_weights.size() != m || g.rev_edge.size() != m) {
        return "reverse CSR size mismatch";
    }
    if (g.rev_offsets.front() != 0 || g.rev_offsets.back() != m) return "reverse offsets broken";
    std::vector<std::uint32_t> seen(m, 0);
    for (NodeId v = 0; v < n; ++v) {
        if (g.rev_offsets[v] > g.rev_offsets[v + 1]) return "reverse offsets not monotone";
        for (std::uint32_t r = g.rev_offsets[v]; r < g.rev_offsets[v + 1]; ++r) {
            const std::uint32_t e = g.rev_edge[r];
            if (e >= m || g.targets[e] != v || g.weights[e] != g.rev_weights[r]) {
                return "reverse entry does not match its forward edge";
            }
            const NodeId u = g.rev_sources[r];
            if (u >= n || e < g.offsets[u] || e >= g.offsets[u + 1]) {
                return "reverse source does not own forward edge";
            }
            if (seen[e]++) return "forward edge referenced twice by reverse CSR";
        }
    }
    return {};
}

bool is_strongly_connected(const Graph& g) {
    const std::uint32_t n = g.num_nodes();
    if (n == 0) return true;
    auto bfs = [&](bool forward) {
        std::vector<char> vis(n, 0);
        std::vector<NodeId> q{0};
        vis[0] = 1;
        std::size_t head = 0;
        while (head < q.size()) {
            const NodeId u = q[head++];
            const auto& off = forward ? g.offsets : g.rev_offsets;
            const auto& adj = forward ? g.targets : g.rev_sources;
            for (std::uint32_t e = off[u]; e < off[u + 1]; ++e) {
                if (!vis[adj[e]]) {
                    vis[adj[e]] = 1;
                    q.push_back(adj[e]);
                }
            }
        }
        return q.size() == n;
    };
    return bfs(true) && bfs(false);
}

// ---------------------------------------------------------------- serialization

namespace {

class Writer {
public:
    template <class T>
    void pod(const T& v) {
        buf_.append(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <class T>
    void array(const std::vector<T>& v) {
        if (!v.empty()) buf_.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
    }
    void bytes(const char* p, std::size_t n) { buf_.append(p, n); }
    std::string& str() { return buf_; }

private:
    std::string buf_;
};

class Reader {
public:
    explicit Reader(const std::string& buf) : buf_(buf) {}
    template <class T>
    T pod() {
        T v;
        need(sizeof(T));
        std::memcpy(&v, buf_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
    }
    template <class T>
    std::vector<T> array(std::size_t count) {
        need(count * sizeof(T));
        std::vector<T> v(count);
        if (count) std::memcpy(v.data(), buf_.data() + pos_, count * sizeof(T));
        pos_ += count * sizeof(T);
        return v;
    }
    std::string string(std::size_t len) {
        need(len);
        std::string s = buf_.substr(pos_, len);
        pos_ += len;
        return s;
    }
    std::size_t pos() const { return pos_; }

private:
    void need(std::size_t n) const {
        if (pos_ + n > buf_.size()) throw std::runtime_error("graph file truncated");
    }
    const std::string& buf_;
    std::size_t pos_ = 0;
};

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open graph file: " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct RawGraph {
    std::uint32_t n = 0, m = 0, max_speed = 0;
    std::string source;
    std::vector<std::uint32_t> offsets, targets, weights, lengths;
    std::vector<std::uint8_t> road_class;
    std::vector<double> lat, lon;
    std::vector<std::uint64_t> ids;
};

RawGraph parse_raw(const std::string& buf) {
    if (buf.size() < sizeof(kMagic) + 8 || std::memcmp(buf.data(), kMagic, sizeof(kMagic)) != 0) {
        throw std::runtime_error("not a DLGRAPH1 file");
    }
    // Verify checksum over everything except the trailing 8 bytes.
    Fnv1a64 h;
    h.update(buf.data(), buf.size() - 8);
    std::uint64_t stored;
    std::memcpy(&stored, buf.data() + buf.size() - 8, 8);
    if (stored != h.digest()) throw std::runtime_error("graph file checksum mismatch");

    Reader r(buf);
    r.string(sizeof(kMagic));
    if (r.pod<std::uint32_t>() != kVersion) throw std::runtime_error("unsupported graph version");
    RawGraph g;
    g.n = r.pod<std::uint32_t>();
    g.m = r.pod<std::uint32_t>();
    g.max_speed = r.pod<std::uint32_t>();
    g.source = r.string(r.pod<std::uint64_t>());
    g.offsets = r.array<std::uint32_t>(static_cast<std::size_t>(g.n) + 1);
    g.targets = r.array<std::uint32_t>(g.m);
    g.weights = r.array<std::uint32_t>(g.m);
    g.lengths = r.array<std::uint32_t>(g.m);
    g.road_class = r.array<std::uint8_t>(g.m);
    g.lat = r.array<double>(g.n);
    g.lon = r.array<double>(g.n);
    g.ids = r.array<std::uint64_t>(g.n);
    if (r.pos() != buf.size() - 8) throw std::runtime_error("graph file has trailing bytes");
    return g;
}

}  // namespace

void save_graph(const Graph& g, const std::string& path) {
    Writer w;
    w.bytes(kMagic, sizeof(kMagic));
    w.pod(kVersion);
    w.pod(g.num_nodes());
    w.pod(g.num_edges());
    w.pod(g.meta.max_speed_kmh);
    w.pod(static_cast<std::uint64_t>(g.meta.source.size()));
    w.bytes(g.meta.source.data(), g.meta.source.size());
    w.array(g.offsets);
    w.array(g.targets);
    w.array(g.weights);
    w.array(g.lengths_mm);
    w.array(g.road_class);
    w.array(g.lat);
    w.array(g.lon);
    w.array(g.osm_ids);
    Fnv1a64 h;
    h.update(w.str().data(), w.str().size());
    w.pod(h.digest());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write graph file: " + path);
    out.write(w.str().data(), static_cast<std::streamsize>(w.str().size()));
    if (!out) throw std::runtime_error("short write: " + path);
}

Graph load_graph(const std::string& path) {
    RawGraph raw = parse_raw(read_file(path));
    Graph g;
    g.offsets = std::move(raw.offsets);
    g.targets = std::move(raw.targets);
    g.weights = std::move(raw.weights);
    g.lengths_mm = std::move(raw.lengths);
    g.road_class = std::move(raw.road_class);
    g.lat = std::move(raw.lat);
    g.lon = std::move(raw.lon);
    g.osm_ids = std::move(raw.ids);
    g.meta.max_speed_kmh = raw.max_speed;
    g.meta.source = std::move(raw.source);
    build_reverse(g);
    const std::string err = validate(g);
    if (!err.empty()) throw std::runtime_error("loaded graph invalid: " + err);
    return g;
}

bool graphs_equal(const Graph& a, const Graph& b) {
    return a.offsets == b.offsets && a.targets == b.targets && a.weights == b.weights &&
           a.lengths_mm == b.lengths_mm && a.road_class == b.road_class &&
           a.rev_offsets == b.rev_offsets && a.rev_sources == b.rev_sources &&
           a.rev_weights == b.rev_weights && a.rev_edge == b.rev_edge && a.lat == b.lat &&
           a.lon == b.lon && a.osm_ids == b.osm_ids &&
           a.meta.max_speed_kmh == b.meta.max_speed_kmh && a.meta.source == b.meta.source;
}

// ---------------------------------------------------------------- adjacency list

std::size_t AdjacencyGraph::memory_bytes() const {
    std::size_t bytes = out.capacity() * sizeof(std::vector<AdjEdge>);
    for (const auto& v : out) bytes += v.capacity() * sizeof(AdjEdge);
    return bytes;
}

AdjacencyGraph to_adjacency(const Graph& g) {
    AdjacencyGraph a;
    a.out.resize(g.num_nodes());
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (std::uint32_t e = g.offsets[u]; e < g.offsets[u + 1]; ++e) {
            a.out[u].push_back({g.targets[e], g.weights[e]});
        }
    }
    return a;
}

AdjacencyGraph load_adjacency(const std::string& path) {
    RawGraph raw = parse_raw(read_file(path));
    AdjacencyGraph a;
    a.out.resize(raw.n);
    for (NodeId u = 0; u < raw.n; ++u) {
        for (std::uint32_t e = raw.offsets[u]; e < raw.offsets[u + 1]; ++e) {
            a.out[u].push_back({raw.targets[e], raw.weights[e]});
        }
    }
    return a;
}

}  // namespace dispatchlab
