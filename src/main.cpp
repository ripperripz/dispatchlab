// DispatchLab command-line interface. Run `dispatchlab help` for usage.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/road_model.hpp"
#include "dispatchlab/routing.hpp"
#include "dispatchlab/scc.hpp"
#ifdef DL_WITH_OSM
#include "dispatchlab/osm_importer.hpp"
#endif

using namespace dispatchlab;

namespace {

// Minimal "--key value" / "--flag" argument parser.
struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> opts;

    Args(int argc, char** argv, int first) {
        for (int i = first; i < argc; ++i) {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0) {
                const std::string key = a.substr(2);
                if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) opts[key] = argv[++i];
                else opts[key] = "1";
            } else {
                positional.push_back(a);
            }
        }
    }
    bool has(const std::string& k) const { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def) const {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
    std::uint64_t num(const std::string& k, std::uint64_t def) const {
        auto it = opts.find(k);
        return it == opts.end() ? def : std::strtoull(it->second.c_str(), nullptr, 10);
    }
    const std::string& pos(std::size_t i, const char* what) const {
        if (i >= positional.size()) throw std::invalid_argument(std::string("missing argument: ") + what);
        return positional[i];
    }
};

double secs_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void usage() {
    std::puts(
        "DispatchLab - deterministic food-delivery routing & dispatch simulator\n"
        "\n"
        "usage: dispatchlab <command> [args]\n"
        "\n"
        "  import <in.osm.pbf> <out.graph> [--clip-tricity] [--source LABEL]\n"
        "      Import drivable roads with libosmium, keep the largest SCC, write CSR binary.\n"
        "  info <graph>\n"
        "      Print graph statistics and run integrity + strong-connectivity checks.\n"
        "  route <graph> <src> <dst> [--algo dijkstra|bidirectional|astar]\n"
        "      Shortest travel time between two compact node ids.\n");
}

int cmd_import(const Args& a) {
#ifdef DL_WITH_OSM
    const std::string in = a.pos(0, "input .osm.pbf");
    const std::string out = a.pos(1, "output .graph");
    ImportOptions opts;
    if (a.has("clip-tricity")) opts.clip = kTricityBBox;
    opts.source_label = a.get("source", in);
    if (opts.clip) {
        char buf[160];
        std::snprintf(buf, sizeof buf, " clip=[%.2f,%.2f,%.2f,%.2f]", opts.clip->min_lon,
                      opts.clip->min_lat, opts.clip->max_lon, opts.clip->max_lat);
        opts.source_label += buf;
    }
    const auto t0 = std::chrono::steady_clock::now();
    ImportStats st;
    Graph g = import_osm(in, opts, &st);
    const double t_import = secs_since(t0);
    save_graph(g, out);
    std::printf(
        "import: highway_ways=%llu used=%llu rejected_access=%llu oneway_fwd=%llu oneway_bwd=%llu "
        "segments_clipped=%llu\n"
        "import: before_scc nodes=%u edges=%u scc_count=%u\n"
        "import: largest_scc nodes=%u edges=%u max_speed_kmh=%u time=%.2fs -> %s\n",
        (unsigned long long)st.highway_ways_seen, (unsigned long long)st.ways_used,
        (unsigned long long)st.ways_rejected_access, (unsigned long long)st.ways_oneway_forward,
        (unsigned long long)st.ways_oneway_backward, (unsigned long long)st.segments_clipped,
        st.nodes_before_scc, st.edges_before_scc, st.scc_count, st.nodes, st.edges, st.max_speed_kmh,
        t_import, out.c_str());
    return 0;
#else
    (void)a;
    std::fprintf(stderr, "built without libosmium (DISPATCHLAB_WITH_OSM=OFF)\n");
    return 1;
#endif
}

int cmd_info(const Args& a) {
    const auto t0 = std::chrono::steady_clock::now();
    Graph g = load_graph(a.pos(0, "graph"));
    const double t_load = secs_since(t0);
    const std::string err = validate(g);
    const bool sc = is_strongly_connected(g);
    const std::uint32_t comps = strongly_connected_components(g).num_components();
    std::uint64_t total_len = 0;
    for (auto l : g.lengths_mm) total_len += l;
    std::vector<std::uint32_t> per_class(kRoadClassCount, 0);
    for (auto c : g.road_class) per_class[c < kRoadClassCount ? c : 0]++;
    bool ids_sorted = true;
    for (std::size_t i = 1; i < g.osm_ids.size(); ++i) {
        if (g.osm_ids[i] <= g.osm_ids[i - 1]) ids_sorted = false;
    }
    std::printf("graph: %s\n", a.pos(0, "graph").c_str());
    std::printf("source: %s\n", g.meta.source.c_str());
    std::printf("nodes=%u edges=%u max_speed_kmh=%u total_length_km=%.1f load_time=%.3fs memory=%.1fMB\n",
                g.num_nodes(), g.num_edges(), g.meta.max_speed_kmh, total_len / 1e6, t_load,
                g.memory_bytes() / 1048576.0);
    for (std::size_t c = 0; c < kRoadClassCount; ++c) {
        if (per_class[c]) std::printf("  class %-15s edges=%u speed=%u km/h\n",
                                      std::string(kRoadClasses[c].highway).c_str(), per_class[c],
                                      kRoadClasses[c].speed_kmh);
    }
    std::printf("validate=%s strongly_connected=%s scc_components=%u osm_ids_sorted_unique=%s\n",
                err.empty() ? "OK" : err.c_str(), sc ? "yes" : "no", comps, ids_sorted ? "yes" : "no");
    return err.empty() && sc && comps == 1 && ids_sorted ? 0 : 1;
}

int cmd_route(const Args& a) {
    Graph g = load_graph(a.pos(0, "graph"));
    const auto s = static_cast<NodeId>(std::stoul(a.pos(1, "src")));
    const auto t = static_cast<NodeId>(std::stoul(a.pos(2, "dst")));
    Router router(g, parse_algorithm(a.get("algo", "astar")));
    const auto t0 = std::chrono::steady_clock::now();
    RouteResult r = router.route(s, t);
    const double us = secs_since(t0) * 1e6;
    if (!r.reachable()) {
        std::printf("unreachable (settled=%u)\n", r.stats.settled);
        return 1;
    }
    std::printf("algo=%s cost_ms=%llu (%.1f min) hops=%zu length_km=%.3f settled=%u time_us=%.0f\n",
                std::string(algorithm_name(router.algorithm())).c_str(), (unsigned long long)r.cost,
                r.cost / 60000.0, r.path.size() - 1, path_length_mm(g, r.path) / 1e6, r.stats.settled, us);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        Args args(argc, argv, 2);
        if (cmd == "import") return cmd_import(args);
        if (cmd == "info") return cmd_info(args);
        if (cmd == "route") return cmd_route(args);
        if (cmd == "help" || cmd == "--help" || cmd == "-h") {
            usage();
            return 0;
        }
        std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
        usage();
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
