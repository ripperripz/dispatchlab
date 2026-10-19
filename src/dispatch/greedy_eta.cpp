#include <algorithm>
#include <numeric>
#include <stdexcept>

#include "dispatchlab/counters.hpp"
#include "dispatchlab/dispatch.hpp"

namespace dispatchlab {

GreedyEtaPolicy::GreedyEtaPolicy(const Graph& g, EtaStrategy strategy, const CCH* cch)
    : g_(g),
      strategy_(strategy),
      to_restaurant_(make_reverse_dijkstra(g)),
      restaurant_to_customer_(g),
      ws_(strategy == EtaStrategy::Pruned ? g.num_nodes() : 0) {
    if (strategy_ == EtaStrategy::Pruned) {
        node_stamp_.assign(g.num_nodes(), 0);
        node_first_.assign(g.num_nodes(), 0);
    }
    if (strategy_ == EtaStrategy::Cch || strategy_ == EtaStrategy::Bounded) {
        cch_ = cch ? std::make_unique<CCH>(*cch) : std::make_unique<CCH>(g);  // a copy shares the hierarchy
    }
}

std::string_view GreedyEtaPolicy::name() const {
    switch (strategy_) {
        case EtaStrategy::Full: return "greedy_eta";
        case EtaStrategy::Pruned: return "greedy_eta_pruned";
        case EtaStrategy::Cch: return "greedy_eta_cch";
        case EtaStrategy::Bounded: return "greedy_eta_bounded";
    }
    return "greedy_eta";
}

// The tie rule shared by every strategy: smaller ETA, then smaller T(c, r), then lower id.
void GreedyEtaPolicy::consider(Best& b, SimTime v, Dist t, std::uint32_t id, std::size_t idx) const {
    if (!b.has || v < b.v || (v == b.v && (t < b.t || (t == b.t && id < b.id)))) {
        b.has = true;
        b.v = v;
        b.t = t;
        b.id = id;
        b.idx = idx;
    }
}

void GreedyEtaPolicy::choose_full(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b) {
    targets_.clear();
    for (const auto& c : av) targets_.push_back(c.location);
    QueryStats st;
    const std::vector<Dist> to_r = to_restaurant_.one_to_many(req.restaurant, targets_, &st);
    ++routing_queries_;
    last_.nodes_settled += st.settled;
    for (std::size_t i = 0; i < av.size(); ++i) {
        if (to_r[i] == kInfDist) continue;
        ++last_.exact_distances;
        consider(b, std::max(req.now + static_cast<SimTime>(to_r[i]), req.ready_at), to_r[i], av[i].id, i);
    }
}

// Backward Dijkstra from the restaurant, stopped by the decision rather than by
// coverage. When the heap's minimum key is d, every unsettled courier has
// T >= d, hence ETA >= max(now + d, t_ready) + T(r, d). Stop when that bound
// exceeds the best ETA found, or equals it while d already exceeds the best
// courier's T (then the T tie-break also favours the incumbent). Stale heap
// entries only make d smaller, so the bound stays valid.
void GreedyEtaPolicy::choose_pruned(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b) {
    count_routing_call();
    ++routing_queries_;
    if (++stamp_ == 0) {
        std::fill(node_stamp_.begin(), node_stamp_.end(), 0);
        stamp_ = 1;
    }
    // node -> couriers there (ascending id), via a sorted array.
    by_node_.clear();
    for (std::size_t i = 0; i < av.size(); ++i) by_node_.push_back({av[i].location, i});
    std::sort(by_node_.begin(), by_node_.end(), [&](const auto& x, const auto& y) {
        return x.first != y.first ? x.first < y.first : av[x.second].id < av[y.second].id;
    });
    for (std::size_t k = 0; k < by_node_.size(); ++k) {
        const NodeId v = by_node_[k].first;
        if (node_stamp_[v] != stamp_) {
            node_stamp_[v] = stamp_;
            node_first_[v] = static_cast<std::uint32_t>(k);
        }
    }
    std::size_t remaining = av.size();
    ws_.reset();
    heap_.clear();
    ws_.set(req.restaurant, 0, kInvalidNode);
    heap_.push({0, req.restaurant});
    while (!heap_.empty() && remaining > 0) {
        const Dist d = heap_.top().key;
        if (b.has) {
            const SimTime lb = std::max(req.now + static_cast<SimTime>(d), req.ready_at);
            if (lb > b.v || (lb == b.v && d > b.t)) break;  // no unsettled courier can win
        }
        const HeapItem top = heap_.take();
        if (top.key != ws_.dist(top.node)) continue;
        ++last_.nodes_settled;
        const NodeId v = top.node;
        if (node_stamp_[v] == stamp_) {
            for (std::size_t k = node_first_[v]; k < by_node_.size() && by_node_[k].first == v; ++k) {
                const std::size_t i = by_node_[k].second;
                ++last_.exact_distances;
                --remaining;
                consider(b, std::max(req.now + static_cast<SimTime>(top.key), req.ready_at), top.key, av[i].id, i);
            }
        }
        for (std::uint32_t e = g_.rev_offsets[v]; e < g_.rev_offsets[v + 1]; ++e) {
            const NodeId u = g_.rev_sources[e];
            const Dist nd = sat_add(top.key, g_.rev_weights[e]);
            if (nd < ws_.dist(u)) {
                ws_.set(u, nd, v);
                heap_.push({nd, u});
            }
        }
    }
}

void GreedyEtaPolicy::choose_cch(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b) {
    last_.nodes_settled += cch_->prepare_target(req.restaurant);
    ++routing_queries_;
    for (std::size_t i = 0; i < av.size(); ++i) {
        std::uint32_t settled = 0;
        const Dist t = cch_->distance_from_prepared(av[i].location, &settled);
        last_.nodes_settled += settled;
        if (t == kInfDist) continue;
        ++last_.exact_distances;
        consider(b, std::max(req.now + static_cast<SimTime>(t), req.ready_at), t, av[i].id, i);
    }
}

// Decision-aware evaluation with geodesic lower bounds. LBt(c) <= T(c, r) is the
// chord bound (the A* heuristic). Couriers are visited by (LB_v, LBt, id) where
// LB_v = max(now + LBt, t_ready). Once LB_v exceeds the best value, or equals it
// while LBt exceeds the best courier's T, every remaining courier provably loses
// (its ETA is larger, or equal with a larger T), so no exact distance is computed
// for them.
void GreedyEtaPolicy::choose_bounded(const DispatchRequest& req, const std::vector<CourierCandidate>& av, Best& b) {
    struct Cand {
        SimTime lbv;
        Dist lbt;
        std::uint32_t id;
        std::size_t idx;
    };
    std::vector<Cand> cands;
    cands.reserve(av.size());
    for (std::size_t i = 0; i < av.size(); ++i) {
        const Dist lbt = restaurant_to_customer_.heuristic(av[i].location, req.restaurant);
        cands.push_back({std::max(req.now + static_cast<SimTime>(lbt), req.ready_at), lbt, av[i].id, i});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        if (x.lbv != y.lbv) return x.lbv < y.lbv;
        if (x.lbt != y.lbt) return x.lbt < y.lbt;
        return x.id < y.id;
    });
    bool prepared = false;
    for (const Cand& c : cands) {
        if (b.has && (c.lbv > b.v || (c.lbv == b.v && c.lbt > b.t))) break;
        if (!prepared) {
            last_.nodes_settled += cch_->prepare_target(req.restaurant);
            ++routing_queries_;
            prepared = true;
        }
        std::uint32_t settled = 0;
        const Dist t = cch_->distance_from_prepared(av[c.idx].location, &settled);
        last_.nodes_settled += settled;
        if (t == kInfDist) continue;
        ++last_.exact_distances;
        consider(b, std::max(req.now + static_cast<SimTime>(t), req.ready_at), t, c.id, c.idx);
    }
}

std::uint32_t GreedyEtaPolicy::choose(const DispatchRequest& req, const std::vector<CourierCandidate>& available) {
    if (available.empty()) throw std::invalid_argument("GreedyEtaPolicy: no available courier");
    count_dispatch_call();
    last_ = DecisionInfo{};

    // T(r, d) is the same for every courier; it shifts all ETAs equally but is
    // part of the ETA definition and is reported.
    const RouteResult rd = restaurant_to_customer_.query(req.restaurant, req.customer, false);
    ++routing_queries_;
    if (!rd.reachable()) throw std::runtime_error("GreedyEtaPolicy: customer unreachable from restaurant");

    Best b;
    switch (strategy_) {
        case EtaStrategy::Full: choose_full(req, available, b); break;
        case EtaStrategy::Pruned: choose_pruned(req, available, b); break;
        case EtaStrategy::Cch: choose_cch(req, available, b); break;
        case EtaStrategy::Bounded: choose_bounded(req, available, b); break;
    }
    if (!b.has) throw std::runtime_error("GreedyEtaPolicy: no courier can reach restaurant");
    last_.courier = available[b.idx].id;
    last_.to_restaurant_ms = b.t;
    last_.restaurant_to_customer_ms = rd.cost;
    last_.eta = b.v + static_cast<SimTime>(rd.cost);
    account(available.size());
    return last_.courier;
}

}  // namespace dispatchlab
