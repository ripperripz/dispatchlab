#include <stdexcept>
#include <string>

#include "dispatchlab/counters.hpp"
#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/geo.hpp"

namespace dispatchlab {

std::uint32_t NearestPolicy::choose(const DispatchRequest& req, const std::vector<CourierCandidate>& available) {
    if (available.empty()) throw std::invalid_argument("NearestPolicy: no available courier");
    count_dispatch_call();
    const double rlat = g_.lat[req.restaurant];
    const double rlon = g_.lon[req.restaurant];
    std::uint32_t best = UINT32_MAX;
    double best_m = 0;
    for (const auto& c : available) {
        const double m = haversine_m(g_.lat[c.location], g_.lon[c.location], rlat, rlon);
        // Strict '<' keeps the lowest id among exact ties (input is id-ascending).
        if (best == UINT32_MAX || m < best_m) {
            best = c.id;
            best_m = m;
        }
    }
    last_ = DecisionInfo{};
    last_.courier = best;
    last_.geo_distance_m = best_m;
    account(available.size());
    return best;
}

std::unique_ptr<DispatchPolicy> make_policy(std::string_view name, const Graph& g) {
    if (name == "nearest") return std::make_unique<NearestPolicy>(g);
    if (name == "greedy_eta" || name == "eta") return std::make_unique<GreedyEtaPolicy>(g);
    if (name == "greedy_eta_pruned") return std::make_unique<GreedyEtaPolicy>(g, EtaStrategy::Pruned);
    if (name == "greedy_eta_cch") return std::make_unique<GreedyEtaPolicy>(g, EtaStrategy::Cch);
    if (name == "greedy_eta_bounded") return std::make_unique<GreedyEtaPolicy>(g, EtaStrategy::Bounded);
    if (name == "greedy_eta_adaptive") return std::make_unique<GreedyEtaPolicy>(g, EtaStrategy::Adaptive);
    if (name == "greedy_eta_rphast") return std::make_unique<GreedyEtaPolicy>(g, EtaStrategy::Rphast);
    if (name == "greedy_eta_hybrid") return std::make_unique<GreedyEtaPolicy>(g, EtaStrategy::Hybrid);
    throw std::invalid_argument("unknown dispatch policy: " + std::string(name));
}

}  // namespace dispatchlab
