#include <algorithm>
#include <cmath>

#include "dispatchlab/geo.hpp"
#include "dispatchlab/osm_importer.hpp"
#include "dispatchlab/road_model.hpp"
#include "dispatchlab/routing.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

namespace {

const std::string kTiny = std::string(DL_FIXTURE_DIR) + "/tiny.osm";

NodeId id_of(const Graph& g, std::uint64_t osm) {
    auto it = std::find(g.osm_ids.begin(), g.osm_ids.end(), osm);
    REQUIRE(it != g.osm_ids.end());
    return static_cast<NodeId>(it - g.osm_ids.begin());
}

bool has_osm(const Graph& g, std::uint64_t osm) {
    return std::find(g.osm_ids.begin(), g.osm_ids.end(), osm) != g.osm_ids.end();
}

}  // namespace

TEST_CASE("osm import: drivable filter, SCC and compact id mapping") {
    ImportStats st;
    Graph g = import_osm(kTiny, {}, &st);
    CHECK(validate(g).empty());
    CHECK(g.num_nodes() == 8);
    CHECK(g.num_edges() == 14);
    CHECK(is_strongly_connected(g));
    // Compact ids follow ascending OSM ids.
    CHECK(g.osm_ids == std::vector<std::uint64_t>{1, 2, 3, 4, 5, 9, 10, 11});
    CHECK_FALSE(has_osm(g, 6));   // only reachable via a one-way leaving it
    CHECK_FALSE(has_osm(g, 7));   // footway
    CHECK_FALSE(has_osm(g, 8));   // access=private
    CHECK_FALSE(has_osm(g, 12));  // island
    CHECK(st.ways_rejected_access == 1);
    CHECK(st.scc_count >= 3);
    CHECK(st.nodes_before_scc > g.num_nodes());
}

TEST_CASE("osm import: one-way restrictions are respected") {
    Graph g = import_osm(kTiny, {});
    const NodeId n3 = id_of(g, 3), n4 = id_of(g, 4);
    CHECK(g.edge_weight(n3, n4) != kInfDist);
    CHECK(g.edge_weight(n4, n3) == kInfDist);  // oneway=yes
    const NodeId n9 = id_of(g, 9), n10 = id_of(g, 10), n11 = id_of(g, 11);
    CHECK(g.edge_weight(n9, n10) != kInfDist);  // roundabout direction
    CHECK(g.edge_weight(n10, n9) == kInfDist);
    CHECK(g.edge_weight(n11, n9) != kInfDist);
    CHECK(g.edge_weight(n9, n11) == kInfDist);
    const NodeId n1 = id_of(g, 1), n2 = id_of(g, 2);
    CHECK(g.edge_weight(n1, n2) != kInfDist);
    CHECK(g.edge_weight(n2, n1) != kInfDist);
    // oneway=reversible service road 1-5 must not create edges; 1<->5 exists only via way 104.
    const NodeId n5 = id_of(g, 5);
    CHECK(g.road_class[g.find_edge(n1, n5)] == static_cast<std::uint8_t>(RoadClass::Residential));
}

TEST_CASE("osm import: haversine lengths and class-speed weights") {
    Graph g = import_osm(kTiny, {});
    const NodeId n3 = id_of(g, 3), n4 = id_of(g, 4);
    const double m = haversine_m(30.7020, 76.7800, 30.7020, 76.7810);
    const std::uint32_t e = g.find_edge(n3, n4);
    CHECK(g.weights[e] == travel_time_ms(m, 50));  // primary = 50 km/h
    CHECK(g.lengths_mm[e] == static_cast<std::uint32_t>(std::llround(m * 1000)));
    CHECK(g.road_class[e] == static_cast<std::uint8_t>(RoadClass::Primary));
    CHECK(m == doctest::Approx(95.6).epsilon(0.01));
    // v_max = fastest surviving class (primary, 50) and every edge respects it.
    CHECK(g.meta.max_speed_kmh == 50);
    for (std::uint32_t i = 0; i < g.num_edges(); ++i) CHECK(g.weights[i] > 0);
}

TEST_CASE("osm import: bounding-box clipping and serialization round trip") {
    ImportOptions opts;
    opts.clip = BBox{76.7785, 30.6995, 76.7815, 30.7025};  // excludes the roundabout
    Graph g = import_osm(kTiny, opts);
    CHECK(g.num_nodes() == 5);  // 1,2,3,4,5
    CHECK(is_strongly_connected(g));
    const std::string path = fixtures::temp_path("tiny.graph");
    save_graph(g, path);
    Graph h = load_graph(path);
    CHECK(graphs_equal(g, h));
    std::remove(path.c_str());
}

TEST_CASE("osm import: shortest path on imported fixture") {
    Graph g = import_osm(kTiny, {});
    Dijkstra d = make_dijkstra(g);
    AStar a(g);
    BidirectionalDijkstra b(g);
    const NodeId n4 = id_of(g, 4), n3 = id_of(g, 3);
    // 4 -> 3 must go around: 4-1-2-3 (one-way blocks 4->3).
    RouteResult r = d.query(n4, n3);
    CHECK(r.path == std::vector<NodeId>{n4, id_of(g, 1), id_of(g, 2), n3});
    CHECK(a.query(n4, n3).cost == r.cost);
    CHECK(b.query(n4, n3).cost == r.cost);
}
