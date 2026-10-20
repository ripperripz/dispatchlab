#include <fstream>

#include "dispatchlab/counters.hpp"
#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/simulator.hpp"
#include "doctest.h"
#include "fixtures.hpp"

using namespace dispatchlab;

namespace {

struct Run {
    Scenario sc;
    SimResult res;
};

Run make_run(std::uint64_t seed, const char* policy, bool paths = false) {
    static const Graph g = fixtures::grid(14, 14);
    ScenarioParams p;
    p.couriers = 6;
    p.orders = 90;
    p.order_window_ms = 2 * 3600000;
    p.max_delivery_km = 2.0;
    p.seed = seed;
    Run r{generate_scenario(g, p), {}};
    auto pol = make_policy(policy, g);
    SimOptions o;
    o.log_paths = paths;
    Simulator sim(g, r.sc, *pol, Algorithm::kAStar, o);
    r.res = sim.run();
    return r;
}

std::vector<LogEvent> through_json(const std::vector<LogEvent>& log, bool paths) {
    std::vector<LogEvent> out;
    for (const auto& e : log) out.push_back(log_event_from_json(log_event_to_json(e, paths)));
    return out;
}

}  // namespace

TEST_CASE("replay: reconstructs the final state hash from the serialized log") {
    for (const char* policy : {"nearest", "greedy_eta"}) {
        Run r = make_run(5, policy);
        const std::vector<LogEvent> parsed = through_json(r.res.log, false);
        const std::uint64_t routing_before = routing_call_counter().load();
        const std::uint64_t dispatch_before = dispatch_call_counter().load();
        ReplayResult rep = replay(r.sc, parsed);
        CHECK(rep.final_state_hash == r.res.final_state_hash);
        CHECK(rep.events == r.res.log.size());
        // Replay performed no routing and no dispatch.
        CHECK(routing_call_counter().load() == routing_before);
        CHECK(dispatch_call_counter().load() == dispatch_before);
    }
}

TEST_CASE("replay: log with paths round-trips through JSON lines file") {
    Run r = make_run(9, "greedy_eta", true);
    const std::string path = fixtures::temp_path("events.jsonl");
    {
        std::ofstream out(path);
        for (const auto& e : r.res.log) out << log_event_to_json(e, true) << "\n";
    }
    std::vector<LogEvent> back = read_event_log(path);
    REQUIRE(back.size() == r.res.log.size());
    for (std::size_t i = 0; i < back.size(); ++i) {
        CHECK(log_event_to_json(back[i], true) == log_event_to_json(r.res.log[i], true));
    }
    CHECK(replay(r.sc, back).final_state_hash == r.res.final_state_hash);
    std::remove(path.c_str());
}

TEST_CASE("replay: different policies on the same scenario give different logs") {
    Run a = make_run(5, "nearest");
    Run b = make_run(5, "greedy_eta");
    CHECK(a.sc.hash() == b.sc.hash());
    // Not guaranteed in general, but with these parameters the decisions differ.
    CHECK(a.res.final_state_hash != b.res.final_state_hash);
}

TEST_CASE("replay: every corrupted log is rejected by the invariant checker") {
    Run r = make_run(5, "nearest");
    auto bad = make_corrupted_logs(r.res.log);
    CHECK(bad.size() == 5);
    for (const auto& [kind, log] : bad) {
        INFO("corruption kind: " << kind);
        CHECK_THROWS_AS(replay(r.sc, log), InvariantViolation);
    }
}

TEST_CASE("replay: truncated log replays to an earlier, valid state") {
    Run r = make_run(5, "nearest");
    std::vector<LogEvent> prefix(r.res.log.begin(), r.res.log.begin() + static_cast<std::ptrdiff_t>(r.res.log.size() / 2));
    ReplayResult rep = replay(r.sc, prefix);
    CHECK(rep.state.delivered <= rep.state.created);
    CHECK(rep.final_state_hash != r.res.final_state_hash);
}

TEST_CASE("replay: events referencing unknown ids or wrong locations are rejected") {
    Run r = make_run(5, "nearest");
    auto log = r.res.log;
    log[0].order = 100000;
    CHECK_THROWS_AS(replay(r.sc, log), InvariantViolation);
    log = r.res.log;
    for (auto& e : log) {
        if (e.type == EventType::OrderDelivered) {
            e.location = e.location == 0 ? 1 : 0;
            break;
        }
    }
    CHECK_THROWS_AS(replay(r.sc, log), InvariantViolation);
}
