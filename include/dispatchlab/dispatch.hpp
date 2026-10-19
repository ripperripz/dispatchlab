#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "dispatchlab/cch.hpp"
#include "dispatchlab/graph.hpp"
#include "dispatchlab/routing.hpp"
#include "dispatchlab/scenario.hpp"

namespace dispatchlab {

struct DispatchRequest {
    std::uint32_t order_id;
    NodeId restaurant;
    NodeId customer;
    SimTime now;
    SimTime ready_at;  // t_ready = created_at + preparation time
};

struct CourierCandidate {
    std::uint32_t id;
    NodeId location;
};

// Details of the most recent decision (for tests and diagnostics).
struct DecisionInfo {
    std::uint32_t courier = UINT32_MAX;
    double geo_distance_m = 0;  // Nearest
    Dist to_restaurant_ms = 0;  // Greedy ETA: T(c, r)
    Dist restaurant_to_customer_ms = 0;  // Greedy ETA: T(r, d)
    SimTime eta = 0;            // Greedy ETA: max(now + T(c,r), t_ready) + T(r,d)
    std::uint64_t nodes_settled = 0;    // search effort spent on this decision
    std::uint32_t exact_distances = 0;  // couriers whose exact T(c, r) was established
};

// A dispatch policy picks one courier from the (non-empty) list of currently
// available couriers. Couriers are supplied in ascending id order; ties are
// broken by the lowest id so decisions are deterministic.
class DispatchPolicy {
public:
    virtual ~DispatchPolicy() = default;
    virtual std::string_view name() const = 0;
    virtual std::uint32_t choose(const DispatchRequest& req, const std::vector<CourierCandidate>& available) = 0;
    const DecisionInfo& last_decision() const { return last_; }
    std::uint64_t routing_queries() const { return routing_queries_; }
    // Totals over all decisions (research metrics).
    std::uint64_t decisions() const { return decisions_; }
    std::uint64_t nodes_settled() const { return nodes_settled_; }
    std::uint64_t exact_distances() const { return exact_distances_; }
    std::uint64_t candidates_seen() const { return candidates_seen_; }

protected:
    void account(std::size_t candidates) {
        ++decisions_;
        nodes_settled_ += last_.nodes_settled;
        exact_distances_ += last_.exact_distances;
        candidates_seen_ += candidates;
    }
    DecisionInfo last_;
    std::uint64_t routing_queries_ = 0;
    std::uint64_t decisions_ = 0, nodes_settled_ = 0, exact_distances_ = 0, candidates_seen_ = 0;
};

// FR-08: argmin over available couriers of the geodesic (haversine) distance
// to the restaurant. Uses no road routing for the decision.
class NearestPolicy final : public DispatchPolicy {
public:
    explicit NearestPolicy(const Graph& g) : g_(g) {}
    std::string_view name() const override { return "nearest"; }
    std::uint32_t choose(const DispatchRequest& req, const std::vector<CourierCandidate>& available) override;

private:
    const Graph& g_;
};

// FR-09: argmin over available couriers of
//   ETA(c, o) = max(now + T(c, r), t_ready) + T(r, d)
// with ties broken by smaller T(c, r), then lower courier id. T(r, d) comes
// from one exact A* query. How T(c, r) is obtained is the research variable
// (docs/RESEARCH.md); every strategy returns the identical courier:
//   Full    - one backward Dijkstra from the restaurant until every available
//             courier is settled (the original baseline)
//   Pruned  - the same search, stopped as soon as the frontier's lower bound
//             max(now + d, t_ready) proves no unsettled courier can win
//   Cch     - CCH backward upward search from the restaurant + one pruned
//             forward upward search per courier (bucket-style one-to-many)
//   Bounded - couriers ordered by an admissible geodesic lower bound; exact
//             CCH distances computed lazily until the bound proves the rest lose
//   Adaptive- Pruned when available couriers are dense (expected search radius
//             n / k nodes below kAdaptiveNodesPerCourier), Bounded otherwise.
//             Exact either way; the rule only affects speed.
enum class EtaStrategy { Full, Pruned, Cch, Bounded, Adaptive };
inline constexpr double kAdaptiveNodesPerCourier = 20000.0;  // tuned on Tricity (docs/RESEARCH.md)

class GreedyEtaPolicy final : public DispatchPolicy {
public:
    // `cch` (optional) is a customized hierarchy for `g` to share; the Cch and
    // Bounded strategies build one if none is given.
    explicit GreedyEtaPolicy(const Graph& g, EtaStrategy strategy = EtaStrategy::Full,
                             const CCH* cch = nullptr);
    std::string_view name() const override;
    std::uint32_t choose(const DispatchRequest& req, const std::vector<CourierCandidate>& available) override;
    EtaStrategy strategy() const { return strategy_; }

private:
    struct Best {
        bool has = false;
        SimTime v = 0;  // max(now + T, t_ready): ETA without the constant T(r, d)
        Dist t = 0;
        std::uint32_t id = 0;
        std::size_t idx = 0;
    };
    void consider(Best& b, SimTime v, Dist t, std::uint32_t id, std::size_t idx) const;
    void choose_full(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b);
    void choose_pruned(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b);
    void choose_cch(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b);
    void choose_bounded(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b);

    const Graph& g_;
    EtaStrategy strategy_;
    ReverseDijkstra to_restaurant_;
    AStar restaurant_to_customer_;
    std::vector<NodeId> targets_;
    // Pruned: own reverse search state plus a node -> couriers index.
    SearchWorkspace ws_;
    RoutingHeap heap_;
    std::vector<std::uint32_t> node_stamp_, node_first_;
    std::uint32_t stamp_ = 0;
    std::vector<std::pair<NodeId, std::size_t>> by_node_;
    // Cch / Bounded.
    std::unique_ptr<CCH> cch_;
};

std::unique_ptr<DispatchPolicy> make_policy(std::string_view name, const Graph& g);

}  // namespace dispatchlab
