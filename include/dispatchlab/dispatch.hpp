#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

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

protected:
    DecisionInfo last_;
    std::uint64_t routing_queries_ = 0;
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
// T(c, r) for all couriers comes from ONE backward Dijkstra from the restaurant
// on the reverse graph (stops once every available courier is settled); T(r, d)
// from one exact A* query. Ties: smaller T(c, r), then lower courier id.
class GreedyEtaPolicy final : public DispatchPolicy {
public:
    explicit GreedyEtaPolicy(const Graph& g);
    std::string_view name() const override { return "greedy_eta"; }
    std::uint32_t choose(const DispatchRequest& req, const std::vector<CourierCandidate>& available) override;

private:
    ReverseDijkstra to_restaurant_;
    AStar restaurant_to_customer_;
    std::vector<NodeId> targets_;
};

std::unique_ptr<DispatchPolicy> make_policy(std::string_view name, const Graph& g);

}  // namespace dispatchlab
