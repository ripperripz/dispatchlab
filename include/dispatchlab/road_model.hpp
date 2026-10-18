#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace dispatchlab {

// OSM highway classes considered drivable for a delivery vehicle.
enum class RoadClass : std::uint8_t {
    Motorway,
    MotorwayLink,
    Trunk,
    TrunkLink,
    Primary,
    PrimaryLink,
    Secondary,
    SecondaryLink,
    Tertiary,
    TertiaryLink,
    Unclassified,
    Residential,
    LivingStreet,
    Service,
    Road,
    Count
};

inline constexpr std::size_t kRoadClassCount = static_cast<std::size_t>(RoadClass::Count);

struct RoadClassInfo {
    std::string_view highway;  // OSM highway=* value
    std::uint32_t speed_kmh;   // documented default speed (see docs/DATA.md)
    bool arterial;             // eligible for congestion multipliers in traffic scenarios
};

// Default speeds per highway class. OSM maxspeed tags are deliberately ignored:
// they are sparse in the Tricity extract and the model must stay uniform and
// documented. Values approximate urban two-wheeler delivery speeds.
inline constexpr std::array<RoadClassInfo, kRoadClassCount> kRoadClasses{{
    {"motorway", 80, true},
    {"motorway_link", 50, true},
    {"trunk", 60, true},
    {"trunk_link", 40, true},
    {"primary", 50, true},
    {"primary_link", 35, true},
    {"secondary", 40, true},
    {"secondary_link", 30, true},
    {"tertiary", 35, false},
    {"tertiary_link", 25, false},
    {"unclassified", 25, false},
    {"residential", 20, false},
    {"living_street", 10, false},
    {"service", 15, false},
    {"road", 20, false},
}};

inline const RoadClassInfo& road_class_info(RoadClass c) {
    return kRoadClasses[static_cast<std::size_t>(c)];
}

inline std::optional<RoadClass> parse_highway(std::string_view v) {
    for (std::size_t i = 0; i < kRoadClassCount; ++i) {
        if (kRoadClasses[i].highway == v) return static_cast<RoadClass>(i);
    }
    return std::nullopt;
}

}  // namespace dispatchlab
