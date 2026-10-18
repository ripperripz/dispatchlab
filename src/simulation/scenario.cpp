#include "dispatchlab/scenario.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "dispatchlab/geo.hpp"
#include "dispatchlab/hash.hpp"
#include "dispatchlab/json.hpp"
#include "dispatchlab/rng.hpp"

namespace dispatchlab {

std::uint64_t Scenario::hash() const {
    Fnv1a64 h;
    h.add_str("DLSCEN1");
    h.add_u32(params.couriers);
    h.add_u32(params.orders);
    h.add_u32(params.restaurants);
    h.add_i64(params.order_window_ms);
    h.add_i64(params.prep_min_ms);
    h.add_i64(params.prep_max_ms);
    h.add_i64(params.drain_ms);
    h.add_i64(std::llround(params.max_delivery_km * 1000.0));
    h.add_str(params.traffic);
    h.add_u64(params.seed);
    h.add_str(graph_source);
    h.add_u32(graph_nodes);
    h.add_i64(end_time);
    h.add_u64(restaurants.size());
    for (const auto& r : restaurants) {
        h.add_u32(r.id);
        h.add_u32(r.location);
    }
    h.add_u64(customers.size());
    for (const auto& c : customers) {
        h.add_u32(c.id);
        h.add_u32(c.location);
    }
    h.add_u64(couriers.size());
    for (const auto& c : couriers) {
        h.add_u32(c.id);
        h.add_u32(c.location);
    }
    h.add_u64(orders.size());
    for (const auto& o : orders) {
        h.add_u32(o.id);
        h.add_u32(o.restaurant_id);
        h.add_u32(o.customer_id);
        h.add_i64(o.created_at);
        h.add_i64(o.preparation_ms);
    }
    return h.digest();
}

Scenario generate_scenario(const Graph& g, const ScenarioParams& p_in) {
    if (g.num_nodes() < 2) throw std::invalid_argument("generate_scenario: graph too small");
    if (p_in.couriers == 0) throw std::invalid_argument("generate_scenario: need at least one courier");
    if (p_in.prep_min_ms > p_in.prep_max_ms || p_in.order_window_ms <= 0) {
        throw std::invalid_argument("generate_scenario: bad timing parameters");
    }
    ScenarioParams p = p_in;
    if (p.restaurants == 0) p.restaurants = std::clamp<std::uint32_t>(p.orders / 10, 5, 200);

    Scenario s;
    s.params = p;
    s.graph_source = g.meta.source;
    s.graph_nodes = g.num_nodes();
    s.end_time = p.order_window_ms + p.drain_ms;

    Rng rng(p.seed);
    const std::uint32_t n = g.num_nodes();
    auto random_node = [&] { return static_cast<NodeId>(rng.below(n)); };

    for (std::uint32_t i = 0; i < p.restaurants; ++i) s.restaurants.push_back({i, random_node()});
    for (std::uint32_t i = 0; i < p.couriers; ++i) s.couriers.push_back({i, random_node()});

    // Arrival times: `orders` i.i.d. uniform points on [0, window), i.e. a
    // Poisson process conditioned on its count. Integer-only => portable.
    struct Draft {
        SimTime created;
        std::uint32_t seq;
        std::uint32_t restaurant;
        SimTime prep;
        NodeId customer_node;
    };
    std::vector<Draft> drafts;
    drafts.reserve(p.orders);
    const double max_m = p.max_delivery_km * 1000.0;
    for (std::uint32_t i = 0; i < p.orders; ++i) {
        Draft d;
        d.seq = i;
        d.created = static_cast<SimTime>(rng.below(static_cast<std::uint64_t>(p.order_window_ms)));
        d.restaurant = static_cast<std::uint32_t>(rng.below(p.restaurants));
        d.prep = rng.range(p.prep_min_ms, p.prep_max_ms);
        const NodeId rnode = s.restaurants[d.restaurant].location;
        // Customer: uniform node within max_delivery_km of the restaurant (rejection
        // sampling); falls back to the closest candidate after 2000 tries.
        NodeId best = random_node();
        double best_m = haversine_m(g.lat[rnode], g.lon[rnode], g.lat[best], g.lon[best]);
        for (int tries = 0; tries < 2000 && !(best_m <= max_m && best != rnode); ++tries) {
            const NodeId c = random_node();
            const double m = haversine_m(g.lat[rnode], g.lon[rnode], g.lat[c], g.lon[c]);
            if ((m <= max_m && c != rnode) || m < best_m) {
                best = c;
                best_m = m;
            }
        }
        d.customer_node = best;
        drafts.push_back(d);
    }
    std::sort(drafts.begin(), drafts.end(), [](const Draft& a, const Draft& b) {
        return a.created != b.created ? a.created < b.created : a.seq < b.seq;
    });
    // Ids follow arrival order; customer i belongs to order i.
    for (std::uint32_t i = 0; i < drafts.size(); ++i) {
        s.customers.push_back({i, drafts[i].customer_node});
        s.orders.push_back({i, drafts[i].restaurant, i, drafts[i].created, drafts[i].prep});
    }
    return s;
}

std::string scenario_to_json(const Scenario& s) {
    json::Writer w;
    w.begin_object();
    w.kv("format", "dispatchlab-scenario-1");
    w.key("params").begin_object();
    w.kv("couriers", s.params.couriers);
    w.kv("orders", s.params.orders);
    w.kv("restaurants", s.params.restaurants);
    w.kv("order_window_ms", static_cast<std::int64_t>(s.params.order_window_ms));
    w.kv("prep_min_ms", static_cast<std::int64_t>(s.params.prep_min_ms));
    w.kv("prep_max_ms", static_cast<std::int64_t>(s.params.prep_max_ms));
    w.kv("drain_ms", static_cast<std::int64_t>(s.params.drain_ms));
    w.kv("max_delivery_m", static_cast<std::int64_t>(std::llround(s.params.max_delivery_km * 1000.0)));
    w.kv("traffic", s.params.traffic);
    w.kv("seed", s.params.seed);
    w.end_object();
    w.kv("graph_source", s.graph_source);
    w.kv("graph_nodes", s.graph_nodes);
    w.kv("end_time", static_cast<std::int64_t>(s.end_time));
    auto pairs = [&](const char* name, const auto& v) {
        w.key(name).begin_array();
        for (const auto& x : v) {
            w.begin_array().value(x.id).value(x.location).end_array();
        }
        w.end_array();
    };
    pairs("restaurants", s.restaurants);
    pairs("customers", s.customers);
    pairs("couriers", s.couriers);
    w.key("orders").begin_array();
    for (const auto& o : s.orders) {
        w.begin_array()
            .value(o.id)
            .value(o.restaurant_id)
            .value(o.customer_id)
            .value(static_cast<std::int64_t>(o.created_at))
            .value(static_cast<std::int64_t>(o.preparation_ms))
            .end_array();
    }
    w.end_array();
    w.kv("hash", hex64(s.hash()));
    w.end_object();
    return w.take();
}

Scenario scenario_from_json(const std::string& text) {
    const json::Value v = json::parse(text);
    if (v.get_string("format", "") != "dispatchlab-scenario-1") throw std::runtime_error("not a scenario file");
    Scenario s;
    const auto& p = v.at("params");
    s.params.couriers = static_cast<std::uint32_t>(p.at("couriers").as_int());
    s.params.orders = static_cast<std::uint32_t>(p.at("orders").as_int());
    s.params.restaurants = static_cast<std::uint32_t>(p.at("restaurants").as_int());
    s.params.order_window_ms = p.at("order_window_ms").as_int();
    s.params.prep_min_ms = p.at("prep_min_ms").as_int();
    s.params.prep_max_ms = p.at("prep_max_ms").as_int();
    s.params.drain_ms = p.at("drain_ms").as_int();
    s.params.max_delivery_km = static_cast<double>(p.at("max_delivery_m").as_int()) / 1000.0;
    s.params.traffic = p.at("traffic").as_string();
    s.params.seed = static_cast<std::uint64_t>(p.at("seed").as_int());
    s.graph_source = v.at("graph_source").as_string();
    s.graph_nodes = static_cast<std::uint32_t>(v.at("graph_nodes").as_int());
    s.end_time = v.at("end_time").as_int();
    auto u32 = [](const json::Value& x) { return static_cast<std::uint32_t>(x.as_int()); };
    for (const auto& r : v.at("restaurants").as_array()) s.restaurants.push_back({u32(r.as_array()[0]), u32(r.as_array()[1])});
    for (const auto& c : v.at("customers").as_array()) s.customers.push_back({u32(c.as_array()[0]), u32(c.as_array()[1])});
    for (const auto& c : v.at("couriers").as_array()) s.couriers.push_back({u32(c.as_array()[0]), u32(c.as_array()[1])});
    for (const auto& o : v.at("orders").as_array()) {
        const auto& a = o.as_array();
        s.orders.push_back({u32(a[0]), u32(a[1]), u32(a[2]), a[3].as_int(), a[4].as_int()});
    }
    if (v.has("hash") && v.at("hash").as_string() != hex64(s.hash())) {
        throw std::runtime_error("scenario hash mismatch (file modified?)");
    }
    // Structural checks: ids dense and in order, references valid.
    for (std::uint32_t i = 0; i < s.orders.size(); ++i) {
        const auto& o = s.orders[i];
        if (o.id != i || o.restaurant_id >= s.restaurants.size() || o.customer_id >= s.customers.size()) {
            throw std::runtime_error("scenario: invalid order record");
        }
        if (i > 0 && o.created_at < s.orders[i - 1].created_at) throw std::runtime_error("scenario: orders not sorted");
    }
    for (std::uint32_t i = 0; i < s.couriers.size(); ++i) {
        if (s.couriers[i].id != i) throw std::runtime_error("scenario: courier ids not dense");
    }
    return s;
}

void save_scenario(const Scenario& s, const std::string& path) { json::write_file(path, scenario_to_json(s)); }
Scenario load_scenario(const std::string& path) { return scenario_from_json(json::read_file(path)); }

}  // namespace dispatchlab
