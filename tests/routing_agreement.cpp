// Routing agreement suite (PRD §13.1): for N seeded random source-destination
// pairs, Dijkstra, Bidirectional Dijkstra and A* must return identical integer
// costs, and each reconstructed path's edge-weight sum must equal its cost.
//
// usage: routing_agreement <graph> [--queries N] [--seed S] [--threads T]
// Exits 0 only if every query agrees. Queries are split across threads; each
// thread owns its own search workspaces, and query i is always pair i of the
// seeded sequence, so the checked set is independent of the thread count.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/hash.hpp"
#include "dispatchlab/rng.hpp"
#include "dispatchlab/routing.hpp"

using namespace dispatchlab;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <graph> [--queries N] [--seed S] [--threads T]\n", argv[0]);
        return 2;
    }
    std::string path = argv[1];
    std::uint64_t queries = 10000, seed = 20261018;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--queries")) queries = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--seed")) seed = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--threads")) threads = static_cast<unsigned>(std::atoi(argv[i + 1]));
        else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    threads = std::max(1u, threads);

    const auto t0 = std::chrono::steady_clock::now();
    Graph g = load_graph(path);
    const std::uint32_t n = g.num_nodes();
    if (n < 2) {
        std::fprintf(stderr, "graph too small\n");
        return 1;
    }

    // Pre-generate the pairs from the seed (deterministic, independent of threads).
    std::vector<std::pair<NodeId, NodeId>> pairs(queries);
    Rng rng(seed);
    for (auto& p : pairs) {
        p.first = static_cast<NodeId>(rng.below(n));
        p.second = static_cast<NodeId>(rng.below(n));
    }

    std::atomic<std::uint64_t> next{0}, failures{0}, unreachable{0}, checked{0};
    std::vector<Dist> costs(queries, 0);
    std::mutex io;
    auto worker = [&] {
        Dijkstra dij = make_dijkstra(g);
        BidirectionalDijkstra bi(g);
        AStar astar(g);
        while (true) {
            const std::uint64_t i = next.fetch_add(1);
            if (i >= queries) break;
            const auto [s, t] = pairs[i];
            const RouteResult a = dij.query(s, t);
            const RouteResult b = bi.query(s, t);
            const RouteResult c = astar.query(s, t);
            bool ok = a.cost == b.cost && a.cost == c.cost;
            if (ok && a.reachable()) {
                ok = path_cost(g, a.path) == a.cost && path_cost(g, b.path) == b.cost &&
                     path_cost(g, c.path) == c.cost && a.path.front() == s && b.path.front() == s &&
                     c.path.front() == s && a.path.back() == t && b.path.back() == t && c.path.back() == t;
            }
            if (!a.reachable()) unreachable.fetch_add(1);
            costs[i] = a.cost;
            checked.fetch_add(1);
            if (!ok) {
                failures.fetch_add(1);
                std::lock_guard<std::mutex> lock(io);
                std::fprintf(stderr,
                             "MISMATCH query %llu s=%u t=%u dijkstra=%llu bidir=%llu astar=%llu "
                             "pathsums=%llu/%llu/%llu\n",
                             static_cast<unsigned long long>(i), s, t,
                             static_cast<unsigned long long>(a.cost), static_cast<unsigned long long>(b.cost),
                             static_cast<unsigned long long>(c.cost),
                             static_cast<unsigned long long>(path_cost(g, a.path)),
                             static_cast<unsigned long long>(path_cost(g, b.path)),
                             static_cast<unsigned long long>(path_cost(g, c.path)));
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned k = 0; k < threads; ++k) pool.emplace_back(worker);
    for (auto& th : pool) th.join();

    Fnv1a64 h;
    for (Dist c : costs) h.add_u64(c);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("routing_agreement: graph=%s nodes=%u edges=%u queries=%llu checked=%llu seed=%llu "
                "unreachable=%llu failures=%llu cost_digest=%s threads=%u time=%.1fs\n",
                path.c_str(), n, g.num_edges(), static_cast<unsigned long long>(queries),
                static_cast<unsigned long long>(checked.load()), static_cast<unsigned long long>(seed),
                static_cast<unsigned long long>(unreachable.load()),
                static_cast<unsigned long long>(failures.load()), hex64(h.digest()).c_str(), threads, secs);
    if (checked.load() != queries || failures.load() != 0) return 1;
    std::printf("ROUTING_AGREEMENT_PASS %llu\n", static_cast<unsigned long long>(queries));
    return 0;
}
