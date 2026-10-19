// Performance-engineering study (plan.md A2 + A3):
//  1. priority queues: identical Dijkstra code with a binary heap, a 4-ary heap,
//     a radix heap and std::priority_queue, on one seeded query set;
//  2. graph layout: the same graph relabelled in several node orders (OSM id,
//     random, BFS, DFS, Hilbert curve, nested dissection), with Dijkstra,
//     Bidirectional and A* run on the same queries mapped into each layout.
// Locality proxies are reported next to latency because hardware cache counters
// need Linux perf, which this harness does not assume.
//
// usage: benchmark_layout <graph> [--queries N] [--seed S] [--out file.json] [--skip-nd 1]

#include <cstdio>
#include <numeric>

#include "bench_common.hpp"
#include "dispatchlab/cch.hpp"
#include "dispatchlab/rng.hpp"
#include "dispatchlab/routing.hpp"

using namespace dispatchlab;

namespace {

struct Stats {
    double median = 0, p95 = 0, p99 = 0, mean = 0, settled = 0;
};
Stats summarize(const std::vector<double>& lat, const std::vector<double>& settled) {
    return {percentile(lat, 50), percentile(lat, 95), percentile(lat, 99), mean(lat), percentile(settled, 50)};
}

// Hilbert index of (x, y) on a 2^16 x 2^16 grid.
std::uint64_t hilbert(std::uint32_t x, std::uint32_t y) {
    std::uint64_t d = 0;
    for (std::uint32_t s = 1u << 15; s > 0; s >>= 1) {
        const std::uint32_t rx = (x & s) ? 1 : 0, ry = (y & s) ? 1 : 0;
        d += static_cast<std::uint64_t>(s) * s * ((3 * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) {
                x = s - 1 - x;
                y = s - 1 - y;
            }
            std::swap(x, y);
        }
    }
    return d;
}

// Converts a visiting order (sequence of old ids) into new_id[old] = position.
std::vector<NodeId> from_order(const std::vector<NodeId>& order) {
    std::vector<NodeId> id(order.size());
    for (NodeId i = 0; i < order.size(); ++i) id[order[i]] = i;
    return id;
}

std::vector<NodeId> graph_search_order(const Graph& g, bool bfs) {
    const std::uint32_t n = g.num_nodes();
    // Start near the geographic centre so the order radiates outward.
    double clat = 0, clon = 0;
    for (NodeId v = 0; v < n; ++v) { clat += g.lat[v]; clon += g.lon[v]; }
    clat /= n; clon /= n;
    NodeId start = 0;
    double best = 1e18;
    for (NodeId v = 0; v < n; ++v) {
        const double d = (g.lat[v] - clat) * (g.lat[v] - clat) + (g.lon[v] - clon) * (g.lon[v] - clon);
        if (d < best) { best = d; start = v; }
    }
    std::vector<char> seen(n, 0);
    std::vector<NodeId> order, frontier{start};
    order.reserve(n);
    seen[start] = 1;
    std::size_t head = 0;
    while (order.size() < n) {
        if (bfs ? head >= frontier.size() : frontier.empty()) {
            for (NodeId v = 0; v < n; ++v) if (!seen[v]) { seen[v] = 1; frontier.push_back(v); break; }
        }
        NodeId u;
        if (bfs) u = frontier[head++];
        else { u = frontier.back(); frontier.pop_back(); }
        order.push_back(u);
        auto visit = [&](NodeId v) { if (!seen[v]) { seen[v] = 1; frontier.push_back(v); } };
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) visit(g.targets[e]);
        for (auto e = g.in_begin(u); e < g.in_end(u); ++e) visit(g.rev_sources[e]);
    }
    return from_order(order);
}

}  // namespace

int main(int argc, char** argv) {
    bench::Args args(argc, argv);
    const std::uint64_t queries = args.num("queries", 500);
    const std::uint64_t seed = args.num("seed", 20261018);
    const bool skip_nd = args.num("skip-nd", 0) != 0;
    // --only <prefix>: run just one layout (no heap study), so an external
    // counter tool (e.g. `/usr/bin/time -l`) attributes instructions/cycles to it.
    const std::string only = args.get("only", "");
    const std::string out = args.get("out", "");

    Graph g = load_graph(args.graph);
    const std::uint32_t n = g.num_nodes();
    Rng rng(seed);
    std::vector<std::pair<NodeId, NodeId>> pairs(queries + 10);
    for (auto& p : pairs) p = {static_cast<NodeId>(rng.below(n)), static_cast<NodeId>(rng.below(n))};

    json::Writer w;
    w.begin_object();
    w.kv("benchmark", "layout_and_heaps");
    bench::write_environment(w, g, seed);
    w.kv("queries", queries);

    // ---- 1. priority queues
    if (only.empty()) {
    std::printf("priority-queue study: %llu queries on %u nodes\n", static_cast<unsigned long long>(queries), n);
    {
        Dijkstra bin = make_dijkstra(g);
        Dijkstra4Ary qua{CsrOut{&g}};
        DijkstraRadix rad{CsrOut{&g}};
        DijkstraStdHeap stdh{CsrOut{&g}};
        const char* names[4] = {"binary heap", "4-ary heap", "radix heap", "std::priority_queue"};
        std::vector<double> lat[4], st[4], pushes;
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            const auto [s, t] = pairs[i];
            RouteResult r[4];
            double us[4];
            auto t0 = bench::Clock::now(); r[0] = bin.query(s, t, false); us[0] = bench::us_since(t0);
            t0 = bench::Clock::now(); r[1] = qua.query(s, t, false); us[1] = bench::us_since(t0);
            t0 = bench::Clock::now(); r[2] = rad.query(s, t, false); us[2] = bench::us_since(t0);
            t0 = bench::Clock::now(); r[3] = stdh.query(s, t, false); us[3] = bench::us_since(t0);
            for (int a = 1; a < 4; ++a) {
                if (r[a].cost != r[0].cost) { std::fprintf(stderr, "heap cost mismatch\n"); return 1; }
            }
            if (i < 10) continue;
            for (int a = 0; a < 4; ++a) { lat[a].push_back(us[a]); st[a].push_back(r[a].stats.settled); }
            pushes.push_back(r[0].stats.pushes);
        }
        w.key("heaps").begin_array();
        std::printf("%-22s %10s %10s %10s\n", "heap", "median_us", "p95_us", "p99_us");
        for (int a = 0; a < 4; ++a) {
            const Stats s = summarize(lat[a], st[a]);
            std::printf("%-22s %10.1f %10.1f %10.1f\n", names[a], s.median, s.p95, s.p99);
            w.begin_object().kv("name", names[a]).kv("median_us", s.median).kv("p95_us", s.p95).kv("p99_us", s.p99);
            w.kv("mean_us", s.mean).end_object();
        }
        w.end_array();
        w.kv("median_heap_pushes", percentile(pushes, 50));
    }
    }

    // ---- 2. layouts
    struct Layout {
        const char* name;
        std::vector<NodeId> id;
    };
    std::vector<Layout> layouts;
    {
        std::vector<NodeId> ident(n);
        std::iota(ident.begin(), ident.end(), 0);
        layouts.push_back({"OSM id order (baseline)", ident});
        std::vector<NodeId> rnd = ident;
        Rng r2(seed ^ 0xabcdef);
        for (std::size_t i = rnd.size() - 1; i > 0; --i) std::swap(rnd[i], rnd[r2.below(i + 1)]);
        layouts.push_back({"random (control)", rnd});
        layouts.push_back({"BFS from centre", graph_search_order(g, true)});
        layouts.push_back({"DFS from centre", graph_search_order(g, false)});
        double a = 1e9, b = 1e9, c = -1e9, d = -1e9;
        for (NodeId v = 0; v < n; ++v) { a = std::min(a, g.lon[v]); c = std::max(c, g.lon[v]); b = std::min(b, g.lat[v]); d = std::max(d, g.lat[v]); }
        std::vector<std::pair<std::uint64_t, NodeId>> key(n);
        for (NodeId v = 0; v < n; ++v) {
            const auto x = static_cast<std::uint32_t>((g.lon[v] - a) / (c - a + 1e-12) * 65535.0);
            const auto y = static_cast<std::uint32_t>((g.lat[v] - b) / (d - b + 1e-12) * 65535.0);
            key[v] = {hilbert(x, y), v};
        }
        std::sort(key.begin(), key.end());
        std::vector<NodeId> order(n);
        for (NodeId i = 0; i < n; ++i) order[i] = key[i].second;
        layouts.push_back({"Hilbert curve", from_order(order)});
        if (!skip_nd && (only.empty() || std::string("nested dissection").rfind(only, 0) == 0)) {
            const auto rank = nested_dissection_ranks(g);
            layouts.push_back({"nested dissection", std::vector<NodeId>(rank.begin(), rank.end())});
        }
    }

    w.key("layouts").begin_array();
    std::printf("\nlayout study (%llu queries, same pairs mapped into each layout)\n", static_cast<unsigned long long>(queries));
    std::printf("%-24s %8s %8s | %10s %10s | %10s %10s | %10s %10s\n", "layout", "gap_med", "near8%",
                "dij_med", "dij_p95", "bi_med", "bi_p95", "astar_med", "astar_p95");
    for (const auto& L : layouts) {
        if (!only.empty() && std::string(L.name).rfind(only, 0) != 0) continue;
        const auto t_relabel = bench::Clock::now();
        Graph h = relabel(g, L.id);
        const double relabel_ms = bench::us_since(t_relabel) / 1000.0;
        // Locality proxies: |new(u) - new(v)| over edges, and the share of edges whose
        // endpoints are within 8 ids (one 64-byte line of the 8-byte distance array).
        std::vector<double> gaps;
        gaps.reserve(h.num_edges());
        std::uint64_t near8 = 0;
        for (NodeId u = 0; u < h.num_nodes(); ++u)
            for (auto e = h.out_begin(u); e < h.out_end(u); ++e) {
                const double gap = std::abs(static_cast<double>(h.targets[e]) - u);
                gaps.push_back(gap);
                near8 += gap < 8 ? 1 : 0;
            }
        Dijkstra dj = make_dijkstra(h);
        BidirectionalDijkstra bi(h);
        AStar as(h);
        std::vector<double> ld, lb, la, sd, sb, sa;
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            const NodeId s = L.id[pairs[i].first], t = L.id[pairs[i].second];
            auto t0 = bench::Clock::now(); RouteResult r0 = dj.query(s, t, false); const double u0 = bench::us_since(t0);
            t0 = bench::Clock::now(); RouteResult r1 = bi.query(s, t, false); const double u1 = bench::us_since(t0);
            t0 = bench::Clock::now(); RouteResult r2 = as.query(s, t, false); const double u2 = bench::us_since(t0);
            if (r1.cost != r0.cost || r2.cost != r0.cost) { std::fprintf(stderr, "layout cost mismatch\n"); return 1; }
            if (i < 10) continue;
            ld.push_back(u0); lb.push_back(u1); la.push_back(u2);
            sd.push_back(r0.stats.settled); sb.push_back(r1.stats.settled); sa.push_back(r2.stats.settled);
        }
        const Stats D = summarize(ld, sd), B = summarize(lb, sb), A = summarize(la, sa);
        const double gap_med = percentile(gaps, 50), near = 100.0 * static_cast<double>(near8) / static_cast<double>(h.num_edges());
        std::printf("%-24s %8.0f %7.1f%% | %10.1f %10.1f | %10.1f %10.1f | %10.1f %10.1f\n", L.name, gap_med, near,
                    D.median, D.p95, B.median, B.p95, A.median, A.p95);
        w.begin_object().kv("name", L.name).kv("relabel_ms", relabel_ms);
        w.kv("edge_gap_median", gap_med).kv("edge_gap_mean", mean(gaps)).kv("edges_within_8_ids_pct", near);
        w.kv("dijkstra_median_us", D.median).kv("dijkstra_p95_us", D.p95).kv("dijkstra_p99_us", D.p99);
        w.kv("bidirectional_median_us", B.median).kv("bidirectional_p95_us", B.p95);
        w.kv("astar_median_us", A.median).kv("astar_p95_us", A.p95);
        w.end_object();
    }
    w.end_array();
    w.kv("methodology",
         "same seeded (s,t) pairs for every heap and layout (mapped through the permutation), interleaved per pair, "
         "no path reconstruction, 10 warm-up pairs; locality proxies instead of hardware counters");
    w.kv("peak_rss_bytes", peak_rss_bytes());
    w.end_object();
    if (!out.empty()) json::write_file(out, w.str());
    return 0;
}
