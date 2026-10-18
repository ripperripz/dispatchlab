#include <fstream>
#include <stdexcept>
#include <string>

#include "dispatchlab/json.hpp"
#include "dispatchlab/simulator.hpp"

namespace dispatchlab {

namespace {

bool has_leg(EventType t) { return t == EventType::CourierAssigned || t == EventType::OrderPickedUp; }

std::int64_t id_or_minus1(std::uint32_t v) { return v == kNone ? -1 : static_cast<std::int64_t>(v); }

std::uint32_t id_from(const json::Value& v) {
    const std::int64_t x = v.as_int();
    if (x < -1 || x > static_cast<std::int64_t>(UINT32_MAX - 1)) throw std::runtime_error("event id out of range");
    return x < 0 ? kNone : static_cast<std::uint32_t>(x);
}

}  // namespace

// Canonical, fixed key order. One JSON object per line (JSON Lines).
std::string log_event_to_json(const LogEvent& e, bool with_path) {
    json::Writer w;
    w.begin_object();
    w.kv("seq", e.seq);
    w.kv("timestamp", static_cast<std::int64_t>(e.timestamp));
    w.kv("event", event_name(e.type));
    w.kv("order_id", id_or_minus1(e.order));
    w.kv("courier_id", id_or_minus1(e.courier));
    w.kv("location", id_or_minus1(e.location));
    if (has_leg(e.type)) {
        w.kv("from", id_or_minus1(e.from));
        w.kv("to", id_or_minus1(e.to));
        w.kv("travel_ms", static_cast<std::int64_t>(e.travel_ms));
        w.kv("distance_mm", e.distance_mm);
        if (with_path) {
            w.key("path").begin_array();
            for (NodeId v : e.path) w.value(v);
            w.end_array();
        }
    }
    w.end_object();
    return w.take();
}

LogEvent log_event_from_json(std::string_view line) {
    const json::Value v = json::parse(line);
    LogEvent e;
    e.seq = static_cast<std::uint64_t>(v.at("seq").as_int());
    e.timestamp = v.at("timestamp").as_int();
    e.type = parse_event_name(v.at("event").as_string());
    e.order = id_from(v.at("order_id"));
    e.courier = id_from(v.at("courier_id"));
    e.location = id_from(v.at("location"));
    if (has_leg(e.type)) {
        e.from = id_from(v.at("from"));
        e.to = id_from(v.at("to"));
        e.travel_ms = v.at("travel_ms").as_int();
        e.distance_mm = static_cast<std::uint64_t>(v.at("distance_mm").as_int());
        if (const json::Value* p = v.find("path")) {
            for (const auto& x : p->as_array()) e.path.push_back(static_cast<NodeId>(x.as_int()));
        }
    }
    return e;
}

std::vector<LogEvent> read_event_log(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open event log " + path);
    std::vector<LogEvent> out;
    std::string line;
    std::size_t lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (line.empty()) continue;
        try {
            out.push_back(log_event_from_json(line));
        } catch (const std::exception& ex) {
            throw std::runtime_error(path + ":" + std::to_string(lineno) + ": " + ex.what());
        }
    }
    return out;
}

}  // namespace dispatchlab
