#include <algorithm>
#include <stdexcept>

#include "dispatchlab/counters.hpp"
#include "dispatchlab/dispatch.hpp"

namespace dispatchlab {

GreedyEtaPolicy::GreedyEtaPolicy(const Graph& g)
    : to_restaurant_(make_reverse_dijkstra(g)), restaurant_to_customer_(g) {}

std::uint32_t GreedyEtaPolicy::choose(const DispatchRequest& req, const std::vector<CourierCandidate>& available) {
    if (available.empty()) throw std::invalid_argument("GreedyEtaPolicy: no available courier");
    count_dispatch_call();

    // T(c, r) for every available courier: one search on the reverse graph.
    targets_.clear();
    for (const auto& c : available) targets_.push_back(c.location);
    const std::vector<Dist> to_r = to_restaurant_.one_to_many(req.restaurant, targets_);
    ++routing_queries_;

    // T(r, d) is the same for every courier; it shifts all ETAs equally but is
    // part of the ETA definition and is reported.
    const RouteResult rd = restaurant_to_customer_.query(req.restaurant, req.customer, false);
    ++routing_queries_;
    if (!rd.reachable()) throw std::runtime_error("GreedyEtaPolicy: customer unreachable from restaurant");

    std::size_t best = available.size();
    SimTime best_eta = 0;
    for (std::size_t i = 0; i < available.size(); ++i) {
        if (to_r[i] == kInfDist) continue;  // cannot reach the restaurant
        const SimTime arrive = req.now + static_cast<SimTime>(to_r[i]);
        const SimTime eta = std::max(arrive, req.ready_at) + static_cast<SimTime>(rd.cost);
        if (best == available.size() || eta < best_eta ||
            (eta == best_eta && to_r[i] < to_r[best])) {  // ties: closer in time, then lower id
            best = i;
            best_eta = eta;
        }
    }
    if (best == available.size()) throw std::runtime_error("GreedyEtaPolicy: no courier can reach restaurant");
    last_ = DecisionInfo{};
    last_.courier = available[best].id;
    last_.to_restaurant_ms = to_r[best];
    last_.restaurant_to_customer_ms = rd.cost;
    last_.eta = best_eta;
    return available[best].id;
}

}  // namespace dispatchlab
