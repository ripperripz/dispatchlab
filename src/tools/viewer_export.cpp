#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

#include "dispatchlab/hash.hpp"
#include "dispatchlab/json.hpp"
#include "dispatchlab/road_model.hpp"
#include "dispatchlab/tools.hpp"

namespace dispatchlab {

namespace {

void write_event(json::Writer& w, const LogEvent& e) {
    w.raw(log_event_to_json(e, true));
}

}  // namespace

std::string build_viewer_replay(const Graph& g, const Scenario& sc, const std::vector<LogEvent>& log,
                                const ViewerMeta& meta) {
    const ReplayResult rep = replay(sc, log);  // also validates the log
    double min_lon = 1e9, min_lat = 1e9, max_lon = -1e9, max_lat = -1e9;
    for (NodeId v = 0; v < g.num_nodes(); ++v) {
        min_lon = std::min(min_lon, g.lon[v]);
        max_lon = std::max(max_lon, g.lon[v]);
        min_lat = std::min(min_lat, g.lat[v]);
        max_lat = std::max(max_lat, g.lat[v]);
    }
    json::Writer w;
    w.begin_object();
    w.kv("format", "dispatchlab-replay-1");
    w.key("meta").begin_object();
    w.kv("policy", meta.policy).kv("routing", meta.routing);
    w.kv("scenario_hash", hex64(sc.hash()));
    w.kv("final_state_hash", hex64(rep.final_state_hash));
    w.kv("graph_source", g.meta.source);
    w.kv("traffic", sc.params.traffic).kv("seed", sc.params.seed);
    w.kv("couriers", static_cast<std::uint64_t>(sc.couriers.size()));
    w.kv("orders", static_cast<std::uint64_t>(sc.orders.size()));
    w.kv("end_time", static_cast<std::int64_t>(sc.end_time));
    w.kv("graph_nodes", g.num_nodes());
    w.end_object();
    w.key("bbox").begin_array().value(min_lon, 6).value(min_lat, 6).value(max_lon, 6).value(max_lat, 6).end_array();
    // Node coordinates as a flat [lon, lat, lon, lat, ...] array (5 decimals ~ 1 m).
    w.key("nodes").begin_array();
    for (NodeId v = 0; v < g.num_nodes(); ++v) w.value(g.lon[v], 5).value(g.lat[v], 5);
    w.end_array();
    // Undirected road segments (u < v once) and an arterial flag for styling.
    w.key("edges").begin_array();
    std::vector<std::uint8_t> arterial;
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) {
            const NodeId v = g.targets[e];
            if (u < v || g.find_edge(v, u) == UINT32_MAX) {
                w.value(u).value(v);
                const auto rc = g.road_class[e];
                arterial.push_back(rc < kRoadClassCount && road_class_info(static_cast<RoadClass>(rc)).arterial ? 1 : 0);
            }
        }
    }
    w.end_array();
    w.key("edge_arterial").begin_array();
    for (auto a : arterial) w.value(static_cast<unsigned>(a));
    w.end_array();
    auto pairs = [&](const char* name, const auto& v) {
        w.key(name).begin_array();
        for (const auto& x : v) w.begin_array().value(x.id).value(x.location).end_array();
        w.end_array();
    };
    pairs("restaurants", sc.restaurants);
    pairs("customers", sc.customers);
    pairs("couriers", sc.couriers);
    w.key("orders").begin_array();
    for (const auto& o : sc.orders) {
        w.begin_array().value(o.id).value(o.restaurant_id).value(o.customer_id);
        w.value(static_cast<std::int64_t>(o.created_at)).value(static_cast<std::int64_t>(o.preparation_ms)).end_array();
    }
    w.end_array();
    w.key("events").begin_array();
    for (const auto& e : log) {
        if ((e.type == EventType::CourierAssigned || e.type == EventType::OrderPickedUp) && e.path.empty()) {
            throw std::runtime_error("event log has no route paths; re-run simulate without --no-paths");
        }
        write_event(w, e);
    }
    w.end_array();
    w.end_object();
    return w.take();
}

std::string validate_viewer_replay(const std::string& text, std::string* summary) {
    json::Value v;
    try {
        v = json::parse(text);
    } catch (const std::exception& e) {
        return std::string("invalid JSON: ") + e.what();
    }
    try {
        if (v.get_string("format", "") != "dispatchlab-replay-1") return "wrong format tag";
        const auto& meta = v.at("meta");
        const auto& nodes = v.at("nodes").as_array();
        if (nodes.empty() || nodes.size() % 2) return "nodes array malformed";
        const std::size_t n = nodes.size() / 2;
        if (static_cast<std::size_t>(meta.at("graph_nodes").as_int()) != n) return "graph_nodes != node count";
        for (const auto& x : nodes) {
            if (!x.is_number() || !std::isfinite(x.as_double())) return "non-numeric coordinate";
        }
        const auto& edges = v.at("edges").as_array();
        if (edges.size() % 2) return "edges array malformed";
        for (const auto& x : edges) {
            if (x.as_int() < 0 || static_cast<std::size_t>(x.as_int()) >= n) return "edge endpoint out of range";
        }
        if (v.at("edge_arterial").as_array().size() * 2 != edges.size()) return "edge_arterial size mismatch";

        // Rebuild the scenario from the document and replay the events.
        Scenario sc;
        sc.graph_nodes = static_cast<std::uint32_t>(n);
        sc.end_time = meta.at("end_time").as_int();
        auto entity = [&](const char* key, auto& outv) {
            for (const auto& x : v.at(key).as_array()) {
                const auto& a = x.as_array();
                const auto node = a.at(1).as_int();
                if (node < 0 || static_cast<std::size_t>(node) >= n) throw std::runtime_error(std::string(key) + " node out of range");
                outv.push_back({static_cast<std::uint32_t>(a.at(0).as_int()), static_cast<NodeId>(node)});
            }
        };
        entity("restaurants", sc.restaurants);
        entity("customers", sc.customers);
        entity("couriers", sc.couriers);
        for (const auto& x : v.at("orders").as_array()) {
            const auto& a = x.as_array();
            OrderSpec o{static_cast<std::uint32_t>(a.at(0).as_int()), static_cast<std::uint32_t>(a.at(1).as_int()),
                        static_cast<std::uint32_t>(a.at(2).as_int()), a.at(3).as_int(), a.at(4).as_int()};
            if (o.id != sc.orders.size() || o.restaurant_id >= sc.restaurants.size() ||
                o.customer_id >= sc.customers.size()) {
                return "order record invalid";
            }
            sc.orders.push_back(o);
        }
        if (static_cast<std::size_t>(meta.at("couriers").as_int()) != sc.couriers.size()) return "courier count mismatch";
        if (static_cast<std::size_t>(meta.at("orders").as_int()) != sc.orders.size()) return "order count mismatch";

        std::vector<LogEvent> log;
        std::size_t paths = 0, path_nodes = 0;
        std::set<std::string> kinds;
        for (const auto& ev : v.at("events").as_array()) {
            LogEvent e;
            e.seq = static_cast<std::uint64_t>(ev.at("seq").as_int());
            e.timestamp = ev.at("timestamp").as_int();
            e.type = parse_event_name(ev.at("event").as_string());
            kinds.insert(ev.at("event").as_string());
            auto id = [](const json::Value& x) { return x.as_int() < 0 ? kNone : static_cast<std::uint32_t>(x.as_int()); };
            e.order = id(ev.at("order_id"));
            e.courier = id(ev.at("courier_id"));
            e.location = id(ev.at("location"));
            if (e.type == EventType::CourierAssigned || e.type == EventType::OrderPickedUp) {
                e.from = id(ev.at("from"));
                e.to = id(ev.at("to"));
                e.travel_ms = ev.at("travel_ms").as_int();
                e.distance_mm = static_cast<std::uint64_t>(ev.at("distance_mm").as_int());
                const auto& p = ev.at("path").as_array();
                if (p.empty()) return "travel event without path";
                for (const auto& x : p) {
                    if (x.as_int() < 0 || static_cast<std::size_t>(x.as_int()) >= n) return "path node out of range";
                    e.path.push_back(static_cast<NodeId>(x.as_int()));
                }
                ++paths;
                path_nodes += p.size();
            }
            log.push_back(std::move(e));
        }
        if (log.empty()) return "no events";
        const ReplayResult rep = replay(sc, log);
        if (hex64(rep.final_state_hash) != meta.at("final_state_hash").as_string()) {
            return "replayed hash differs from meta.final_state_hash";
        }
        if (summary) {
            std::ostringstream os;
            os << "nodes=" << n << " segments=" << edges.size() / 2 << " couriers=" << sc.couriers.size()
               << " orders=" << sc.orders.size() << " events=" << log.size() << " event_types=" << kinds.size()
               << " routed_legs=" << paths << " path_nodes=" << path_nodes << " delivered=" << rep.state.delivered
               << " final_state_hash=" << hex64(rep.final_state_hash);
            *summary = os.str();
        }
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

}  // namespace dispatchlab
