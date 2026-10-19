#include <queue>
#include <set>
#include <vector>

#include "dispatchlab/binary_heap.hpp"
#include "dispatchlab/rng.hpp"
#include "doctest.h"

using dispatchlab::BinaryHeap;
using dispatchlab::Rng;

TEST_CASE("heap: empty heap behaviour") {
    BinaryHeap<int> h;
    CHECK(h.empty());
    CHECK(h.size() == 0);
    CHECK_THROWS_AS(h.top(), std::out_of_range);
    CHECK_THROWS_AS(h.pop(), std::out_of_range);
    h.push(5);
    CHECK_FALSE(h.empty());
    CHECK(h.top() == 5);
    h.pop();
    CHECK(h.empty());
}

TEST_CASE("heap: insert and extract in sorted order with duplicates") {
    BinaryHeap<int> h;
    for (int v : {5, 3, 8, 3, 1, 9, 1, 1, 7, 3}) {
        h.push(v);
        REQUIRE(h.is_valid());
    }
    std::vector<int> out;
    while (!h.empty()) {
        out.push_back(h.take());
        REQUIRE(h.is_valid());
    }
    CHECK(out == std::vector<int>{1, 1, 1, 3, 3, 3, 5, 7, 8, 9});
}

TEST_CASE("heap: randomized interleaved ops match std::priority_queue") {
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        Rng rng(seed);
        BinaryHeap<std::uint64_t> h;
        std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<>> ref;
        const int ops = 5000;
        const std::uint64_t range = seed % 2 ? 50 : 1'000'000;  // small range => many duplicates
        for (int i = 0; i < ops; ++i) {
            const bool do_push = ref.empty() || rng.below(100) < 55;
            if (do_push) {
                const std::uint64_t v = rng.below(range);
                h.push(v);
                ref.push(v);
            } else {
                REQUIRE(h.top() == ref.top());
                h.pop();
                ref.pop();
            }
            REQUIRE(h.size() == ref.size());
            REQUIRE(h.empty() == ref.empty());
            if (!ref.empty()) REQUIRE(h.top() == ref.top());
            if (i % 97 == 0) REQUIRE(h.is_valid());
        }
        while (!ref.empty()) {
            REQUIRE(h.take() == ref.top());
            ref.pop();
        }
        CHECK(h.empty());
    }
}

TEST_CASE("heap: custom comparator and structs with tie-breaking") {
    struct Item {
        int key;
        int id;
    };
    struct Less {
        bool operator()(const Item& a, const Item& b) const {
            return a.key < b.key || (a.key == b.key && a.id < b.id);
        }
    };
    BinaryHeap<Item, Less> h;
    h.push({2, 9});
    h.push({1, 4});
    h.push({2, 1});
    h.push({1, 3});
    CHECK(h.take().id == 3);
    CHECK(h.take().id == 4);
    CHECK(h.take().id == 1);
    CHECK(h.take().id == 9);
}

TEST_CASE("heap: max-heap via std::greater and clear()") {
    BinaryHeap<int, std::greater<int>> h;
    for (int i = 0; i < 100; ++i) h.push(i);
    CHECK(h.top() == 99);
    h.clear();
    CHECK(h.empty());
}

#include "dispatchlab/heaps.hpp"
#include "dispatchlab/routing.hpp"
#include "fixtures.hpp"

TEST_CASE("heap: 4-ary heap matches std::priority_queue on random interleaved ops") {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        Rng rng(seed);
        dispatchlab::DaryHeap<std::uint64_t, 4> h;
        std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<>> ref;
        for (int i = 0; i < 4000; ++i) {
            if (ref.empty() || rng.below(100) < 55) {
                const auto v = rng.below(seed % 2 ? 40 : 1000000);
                h.push(v);
                ref.push(v);
            } else {
                REQUIRE(h.take() == ref.top());
                ref.pop();
            }
            REQUIRE(h.size() == ref.size());
            if (i % 101 == 0) REQUIRE(h.is_valid());
        }
    }
}

TEST_CASE("heap: radix heap matches a reference on monotone (Dijkstra-like) sequences") {
    using dispatchlab::HeapItem;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        Rng rng(seed);
        dispatchlab::RadixHeap<HeapItem> h;
        std::multiset<dispatchlab::Dist> ref;
        dispatchlab::Dist last = 0;
        for (int i = 0; i < 5000; ++i) {
            if (ref.empty() || rng.below(100) < 60) {
                const dispatchlab::Dist k = last + rng.below(seed % 2 ? 8 : 5000000);  // >= last taken key
                h.push({k, static_cast<dispatchlab::NodeId>(i)});
                ref.insert(k);
            } else {
                const HeapItem x = h.take();
                REQUIRE(x.key == *ref.begin());
                ref.erase(ref.begin());
                last = x.key;
            }
            REQUIRE(h.size() == ref.size());
        }
        if (last > 0) CHECK_THROWS_AS(h.push({last - 1, 0}), std::logic_error);
    }
}

TEST_CASE("heap: Dijkstra gives identical costs with every heap variant") {
    using namespace dispatchlab;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        Graph g = fixtures::random_geometric(500, 3, seed);
        Dijkstra a = make_dijkstra(g);
        Dijkstra4Ary b{CsrOut{&g}};
        DijkstraRadix c{CsrOut{&g}};
        DijkstraStdHeap d{CsrOut{&g}};
        Rng rng(seed);
        for (int i = 0; i < 300; ++i) {
            const auto s = static_cast<NodeId>(rng.below(g.num_nodes())), t = static_cast<NodeId>(rng.below(g.num_nodes()));
            const RouteResult ra = a.query(s, t);
            REQUIRE(b.query(s, t, false).cost == ra.cost);
            const RouteResult rc = c.query(s, t);
            REQUIRE(rc.cost == ra.cost);
            if (rc.reachable()) REQUIRE(path_cost(g, rc.path) == rc.cost);
            REQUIRE(d.query(s, t, false).cost == ra.cost);
        }
    }
}
