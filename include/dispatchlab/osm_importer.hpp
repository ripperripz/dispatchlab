#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "dispatchlab/graph.hpp"

namespace dispatchlab {

struct BBox {
    double min_lon, min_lat, max_lon, max_lat;
    bool contains(double lat, double lon) const {
        return lat >= min_lat && lat <= max_lat && lon >= min_lon && lon <= max_lon;
    }
};

// Tricity (Chandigarh, Mohali, Panchkula) bounding box used for the dataset.
inline constexpr BBox kTricityBBox{76.62, 30.62, 76.95, 30.80};

struct ImportOptions {
    // If set, only road segments with both endpoints inside the box are kept.
    // Needed because `osmium extract --strategy complete_ways` keeps whole ways
    // that merely cross the box.
    std::optional<BBox> clip;
    std::string source_label;  // provenance stored in GraphMeta::source
};

struct ImportStats {
    std::uint64_t highway_ways_seen = 0;
    std::uint64_t ways_used = 0;
    std::uint64_t ways_rejected_access = 0;
    std::uint64_t ways_oneway_forward = 0;
    std::uint64_t ways_oneway_backward = 0;
    std::uint64_t segments_clipped = 0;
    std::uint32_t nodes_before_scc = 0;
    std::uint32_t edges_before_scc = 0;
    std::uint32_t scc_count = 0;
    std::uint32_t nodes = 0;
    std::uint32_t edges = 0;
    std::uint32_t max_speed_kmh = 0;
};

// Reads an OSM file (.osm.pbf or .osm XML) with libosmium and returns the
// largest strongly connected component of the drivable road graph in CSR form.
// Edge weights: ceil(haversine length / class default speed) in ms.
Graph import_osm(const std::string& path, const ImportOptions& opts, ImportStats* stats = nullptr);

}  // namespace dispatchlab
