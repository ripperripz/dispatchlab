#include <optional>

#include "dispatchlab/simulator.hpp"

namespace dispatchlab {

// Replay applies the logged transitions through the same StateMachine the
// simulator uses, with every invariant checked (paranoid mode). It has no
// access to a graph, router or dispatch policy: travel times, distances and
// assignments are taken from the log, never recomputed.
ReplayResult replay(const Scenario& sc, const std::vector<LogEvent>& log) {
    StateMachine sm(sc, /*paranoid=*/true);
    for (const LogEvent& e : log) sm.apply(e);
    sm.check_global();
    ReplayResult r;
    r.state = sm.state();
    r.final_state_hash = state_hash(r.state);
    r.events = log.size();
    return r;
}

}  // namespace dispatchlab

namespace dispatchlab {

namespace {

void renumber(std::vector<LogEvent>& log) {
    for (std::size_t i = 0; i < log.size(); ++i) log[i].seq = i;
}

std::optional<std::size_t> find_event(const std::vector<LogEvent>& log, EventType t, std::size_t from = 0) {
    for (std::size_t i = from; i < log.size(); ++i) {
        if (log[i].type == t) return i;
    }
    return std::nullopt;
}

}  // namespace

std::vector<std::pair<std::string, std::vector<LogEvent>>> make_corrupted_logs(const std::vector<LogEvent>& log) {
    std::vector<std::pair<std::string, std::vector<LogEvent>>> out;

    // 1. Delivery before pickup: drop the first pickup.
    if (auto i = find_event(log, EventType::OrderPickedUp)) {
        auto bad = log;
        bad.erase(bad.begin() + static_cast<std::ptrdiff_t>(*i));
        renumber(bad);
        out.emplace_back("delivery_before_pickup", std::move(bad));
    }
    // 2. Double delivery: duplicate the first delivery event.
    if (auto i = find_event(log, EventType::OrderDelivered)) {
        auto bad = log;
        bad.insert(bad.begin() + static_cast<std::ptrdiff_t>(*i) + 1, log[*i]);
        renumber(bad);
        out.emplace_back("double_delivery", std::move(bad));
    }
    // 3. Time moving backward.
    for (std::size_t k = 1; k < log.size(); ++k) {
        if (log[k - 1].timestamp > 0) {
            auto bad = log;
            bad[k].timestamp = log[k - 1].timestamp - 1;
            out.emplace_back("time_backwards", std::move(bad));
            break;
        }
    }
    // 4. Courier double assignment: re-target a later assignment to a courier
    //    that is still busy with an earlier order.
    {
        bool done = false;
        for (std::size_t i = 0; i < log.size() && !done; ++i) {
            if (log[i].type != EventType::CourierAssigned) continue;
            const std::uint32_t c = log[i].courier;
            for (std::size_t j = i + 1; j < log.size(); ++j) {
                if (log[j].type == EventType::CourierAvailable && log[j].courier == c) break;  // c free again
                if (log[j].type == EventType::CourierAssigned && log[j].courier != c) {
                    auto bad = log;
                    bad[j].courier = c;
                    out.emplace_back("courier_double_assignment", std::move(bad));
                    done = true;
                    break;
                }
            }
        }
    }
    // 5. Pickup before food ready: for an order whose courier arrived before the
    //    food was ready, move the pickup to right after the arrival.
    {
        bool done = false;
        for (std::size_t a = 0; a < log.size() && !done; ++a) {
            if (log[a].type != EventType::CourierArrivedRestaurant) continue;
            const std::uint32_t o = log[a].order;
            std::optional<std::size_t> ready, pick;
            for (std::size_t j = a + 1; j < log.size(); ++j) {
                if (log[j].order != o) continue;
                if (log[j].type == EventType::FoodReady) ready = j;
                if (log[j].type == EventType::OrderPickedUp) {
                    pick = j;
                    break;
                }
            }
            if (!ready || !pick || log[*ready].timestamp <= log[a].timestamp) continue;
            auto bad = log;
            LogEvent p = bad[*pick];
            p.timestamp = log[a].timestamp;
            bad.erase(bad.begin() + static_cast<std::ptrdiff_t>(*pick));
            bad.insert(bad.begin() + static_cast<std::ptrdiff_t>(a) + 1, p);
            renumber(bad);
            out.emplace_back("pickup_before_food_ready", std::move(bad));
            done = true;
        }
    }
    return out;
}

}  // namespace dispatchlab
