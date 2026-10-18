#include "dispatchlab/osm_importer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <osmium/handler.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/visitor.hpp>

#include "dispatchlab/geo.hpp"
#include "dispatchlab/road_model.hpp"
#include "dispatchlab/scc.hpp"

namespace dispatchlab {

namespace {

enum class Direction : std::uint8_t { Both, Forward, Backward };

struct WayRecord {
    RoadClass road_class;
    Direction dir;
    std::vector<std::uint64_t> refs;
};

bool tag_is(const char* v, std::initializer_list<const char*> options) {
    if (!v) return false;
    for (const char* o : options) {
        if (std::strcmp(v, o) == 0) return true;
    }
    return false;
}

// Drivability rules (documented in docs/DATA.md).
bool access_allowed(const osmium::TagList& tags) {
    const char* motor = tags["motor_vehicle"];
    if (tag_is(motor, {"no", "private"})) return false;
    if (tag_is(tags["access"], {"no", "private"})) {
        const std::initializer_list<const char*> ok = {"yes", "permissive", "designated", "destination"};
        return tag_is(motor, ok) || tag_is(tags["motorcycle"], ok) || tag_is(tags["motorcar"], ok);
    }
    return true;
}

// Returns nullopt for ways whose direction is unpredictable (reversible lanes).
std::optional<Direction> way_direction(const osmium::TagList& tags, RoadClass rc) {
    const char* oneway = tags["oneway"];
    if (tag_is(oneway, {"yes", "true", "1"})) return Direction::Forward;
    if (tag_is(oneway, {"-1", "reverse"})) return Direction::Backward;
    if (tag_is(oneway, {"reversible", "alternating"})) return std::nullopt;
    if (tag_is(oneway, {"no", "false", "0"})) return Direction::Both;
    // Implied one-way: roundabouts and motorways (OSM convention).
    if (tag_is(tags["junction"], {"roundabout", "circular"})) return Direction::Forward;
    if (rc == RoadClass::Motorway || rc == RoadClass::MotorwayLink) return Direction::Forward;
    return Direction::Both;
}

class WayCollector : public osmium::handler::Handler {
public:
    std::vector<WayRecord> ways;
    ImportStats* stats;

    explicit WayCollector(ImportStats* s) : stats(s) {}

    void way(const osmium::Way& w) {
        const osmium::TagList& tags = w.tags();
        const char* hw = tags["highway"];
        if (!hw) return;
        const auto rc = parse_highway(hw);
        if (!rc) return;
        ++stats->highway_ways_seen;
        if (tag_is(tags["area"], {"yes"})) return;
        if (!access_allowed(tags)) {
            ++stats->ways_rejected_access;
            return;
        }
        const auto dir = way_direction(tags, *rc);
        if (!dir) return;
        if (w.nodes().size() < 2) return;
        WayRecord rec{*rc, *dir, {}};
        rec.refs.reserve(w.nodes().size());
        for (const auto& nr : w.nodes()) rec.refs.push_back(static_cast<std::uint64_t>(nr.ref()));
        if (*dir == Direction::Forward) ++stats->ways_oneway_forward;
        if (*dir == Direction::Backward) ++stats->ways_oneway_backward;
        ++stats->ways_used;
        ways.push_back(std::move(rec));
    }
};

class NodeCollector : public osmium::handler::Handler {
public:
    const std::vector<std::uint64_t>& needed;  // sorted unique
    std::vector<double> lat, lon;
    std::vector<char> found;

    explicit NodeCollector(const std::vector<std::uint64_t>& n)
        : needed(n), lat(n.size(), 0.0), lon(n.size(), 0.0), found(n.size(), 0) {}

    void node(const osmium::Node& node) {
        const auto id = static_cast<std::uint64_t>(node.id());
        const auto it = std::lower_bound(needed.begin(), needed.end(), id);
        if (it == needed.end() || *it != id) return;
        if (!node.location().valid()) return;
        const std::size_t i = static_cast<std::size_t>(it - needed.begin());
        lat[i] = node.location().lat();
        lon[i] = node.location().lon();
        found[i] = 1;
    }
};

}  // namespace

Graph import_osm(const std::string& path, const ImportOptions& opts, ImportStats* stats_out) {
    ImportStats stats;

    // Pass 1: drivable ways.
    WayCollector wc(&stats);
    {
        osmium::io::Reader reader(path, osmium::osm_entity_bits::way);
        osmium::apply(reader, wc);
        reader.close();
    }

    // Collect referenced node ids; the sorted order defines the compact id
    // mapping (ascending OSM id), so the mapping is deterministic.
    std::vector<std::uint64_t> needed;
    for (const auto& w : wc.ways) needed.insert(needed.end(), w.refs.begin(), w.refs.end());
    std::sort(needed.begin(), needed.end());
    needed.erase(std::unique(needed.begin(), needed.end()), needed.end());

    // Pass 2: coordinates of referenced nodes.
    NodeCollector nc(needed);
    {
        osmium::io::Reader reader(path, osmium::osm_entity_bits::node);
        osmium::apply(reader, nc);
        reader.close();
    }

    auto index_of = [&](std::uint64_t id) {
        return static_cast<std::uint32_t>(std::lower_bound(needed.begin(), needed.end(), id) - needed.begin());
    };
    auto usable = [&](std::uint32_t i) {
        if (!nc.found[i]) return false;
        return !opts.clip || opts.clip->contains(nc.lat[i], nc.lon[i]);
    };

    std::vector<EdgeInput> edges;
    std::uint32_t max_speed = 0;
    for (const auto& w : wc.ways) {
        const std::uint32_t speed = road_class_info(w.road_class).speed_kmh;
        for (std::size_t k = 1; k < w.refs.size(); ++k) {
            const std::uint32_t a = index_of(w.refs[k - 1]);
            const std::uint32_t b = index_of(w.refs[k]);
            if (a == b) continue;
            if (!usable(a) || !usable(b)) {
                ++stats.segments_clipped;
                continue;
            }
            const double metres = haversine_m(nc.lat[a], nc.lon[a], nc.lat[b], nc.lon[b]);
            const Weight wt = travel_time_ms(metres, speed);
            const auto len_mm = static_cast<std::uint32_t>(std::llround(metres * 1000.0));
            const auto rc = static_cast<std::uint8_t>(w.road_class);
            if (w.dir != Direction::Backward) edges.push_back({a, b, wt, len_mm, rc});
            if (w.dir != Direction::Forward) edges.push_back({b, a, wt, len_mm, rc});
            max_speed = std::max(max_speed, speed);
        }
    }

    const auto n = static_cast<std::uint32_t>(needed.size());
    Graph full = build_csr(n, std::move(edges), nc.lat, nc.lon, needed);
    full.meta.max_speed_kmh = max_speed;
    full.meta.source = opts.source_label.empty() ? path : opts.source_label;
    stats.nodes_before_scc = full.num_nodes();
    stats.edges_before_scc = full.num_edges();
    stats.scc_count = strongly_connected_components(full).num_components();

    Graph g = largest_scc_subgraph(full);
    // Tightest valid v_max: the fastest class that survives SCC extraction.
    std::uint32_t kept_max = 0;
    for (std::uint8_t rc : g.road_class) {
        kept_max = std::max(kept_max, road_class_info(static_cast<RoadClass>(rc)).speed_kmh);
    }
    g.meta.max_speed_kmh = kept_max;
    stats.nodes = g.num_nodes();
    stats.edges = g.num_edges();
    stats.max_speed_kmh = g.meta.max_speed_kmh;

    const std::string err = validate(g);
    if (!err.empty()) throw std::runtime_error("imported graph failed validation: " + err);
    if (stats_out) *stats_out = stats;
    return g;
}

}  // namespace dispatchlab
