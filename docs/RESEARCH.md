# Decision-aware Greedy ETA dispatch

**Question.** A Greedy ETA dispatcher picks the available courier with the earliest estimated
delivery:

```
ETA(c) = max(now + T(c, r), t_ready) + T(r, d)
```

Ties go to the smaller T(c, r), then the lower courier id. The textbook way to evaluate it computes
the exact road travel time T(c, r) from **every** available courier to the restaurant r, for
example with one backward Dijkstra from r until every courier is settled. Can the dispatcher
provably make the *same* decision while doing far less shortest-path work?

**Answer (measured).** Yes. On the Tricity network (142,425 nodes), an exact *adaptive* strategy
makes identical decisions, verified by identical final state hashes in 18 full simulations per
strategy and identical choices in every micro sample (1,200 per strategy; 600 in adaptive's own run). It cuts the mean decision time by **16× in
large, normally loaded scenarios** (7.93 → 0.50 ms) and **7.2× under peak load** (4.55 → 0.63 ms),
while establishing about one exact courier distance per decision instead of up to 250. One
component, a naive CCH one-to-many, is a **loss**: up to 3.4× *slower* than the baseline. It is
reported below with the reason.

This is engineering on top of known ideas (bounded and pruned searches, contraction hierarchies),
not a new algorithmic primitive; see *Related work*. What the project adds is an exact, evaluated
application of them to food-delivery dispatch with preparation times, inside a deterministic
simulator that checks exactness at system level.

## 1. Strategies

All strategies share one tie rule and return the identical courier (§2). T(r, d) is one exact A*
query in every strategy.

| Strategy | How T(c, r) is obtained |
|---|---|
| `full` (baseline) | One backward Dijkstra from r on the reverse graph until every available courier is settled. |
| `pruned` | The same search, **stopped by the decision**: at heap minimum d, every unsettled courier has T ≥ d, so ETA ≥ max(now + d, t_ready) + T(r, d). Stop when that bound exceeds the best ETA found, or equals it while d exceeds the incumbent's T (it then also wins the tie-break). |
| `cch` | A complete backward upward CCH search from r, kept, plus one pruned forward upward search per courier (bucket-style one-to-many). |
| `bounded` | Couriers sorted by an admissible geodesic lower bound LB(c) ≤ T(c, r) (the chord bound used by A*). Exact CCH distances are computed lazily in that order; stop when max(now + LB, t_ready) exceeds the best value, or equals it while LB exceeds the incumbent's T. |
| `adaptive` | `pruned` if the expected search radius n / k (graph nodes / available couriers) is below 20,000 nodes, otherwise `bounded`. Exact either way; the rule only affects speed. |

## 2. Exactness

**Pruned.** Dijkstra settles nodes in non-decreasing distance order, and the heap's minimum key d
(stale entries included) is a lower bound on the distance of every unsettled node. An unsettled
courier c therefore has T(c, r) ≥ d and ETA(c) ≥ max(now + d, t_ready) + T(r, d), because the ETA is
monotone in T. If that bound is greater than the incumbent's ETA, c loses. If it is equal, c can at
best tie on ETA, and then T(c, r) ≥ d > T(incumbent) loses the T tie-break. The id tie-break only
applies when T is also equal, and that case is settled before stopping, because the condition
requires d > T(incumbent). Couriers settled at the same node are visited in ascending id order.

**Bounded.** LB(c) is the chord distance between the two points on the unit sphere (minus a 3 m
float-rounding slack) divided by the graph's v_max. The chord is no longer than the great-circle
distance, which is no longer than any road path, and every edge weight satisfies
w ≥ length · 3600 / v_max (weights are rounded up). So LB(c) ≤ T(c, r), also under traffic
multipliers ≥ 1. Couriers are visited in order of (max(now + LB, t_ready), LB, id), and the stopping
test mirrors the pruned one. Every courier after the stopping point has a bound that already loses.

**Cch.** The backward upward search from r is complete. For any courier, the shortest c → r path has
an up-down form whose peak carries an exact backward label. A forward upward search pruned at the
best meeting cost therefore returns the exact T(c, r). Separately, CCH passes the 10,000-query
agreement suite against Dijkstra.

**Checked, not just argued.**
- Unit tests compare all five strategies on randomized, tie-heavy requests: couriers sharing nodes,
  food far from ready, unreachable couriers.
- A test checks that a full simulation under each strategy produces a byte-identical event log and
  the same final state hash.
- The benchmark aborts on the first differing choice or hash. `EXACTNESS_OK` was printed for every
  run reported here.

## 3. Results (Tricity, 142,425 nodes, Apple M4, AppleClang 21, `-O3`)

Raw data: `benchmarks/results/eta_tricity.json` and `eta_tricity_adaptive.json`; command in §6.

### Micro: one decision, all couriers available at random nodes (median; 200 samples per cell, 100 in the adaptive run)

| Couriers | full | pruned | cch | bounded | adaptive |
|---:|---:|---:|---:|---:|---:|
| 20 (food not ready) | 7.74 ms | 0.50 ms | 2.49 ms | 0.68 ms | 0.53 ms |
| 100 | 8.01 ms | 0.37 ms | 10.9 ms | 0.85 ms | 0.39 ms |
| 500 | 8.05 ms | 0.31 ms | **50.9 ms** | 0.72 ms | 0.35 ms |
| exact T(c, r) at 500 | 500 | 1 | 500 | 11 | 1 |

### System: full simulations, moderate traffic, mean decision latency (mean of 3 seeds; seed-to-seed spread 5–19% of the mean)

| Scenario (couriers / orders / window) | full | pruned | cch | bounded | adaptive | adaptive vs full |
|---|---:|---:|---:|---:|---:|---:|
| small (20 / 100 / 4 h) | 7.00 ms | 1.37 ms | 1.40 ms | 0.90 ms | 0.96 ms | 7.3× |
| small peak (20 / 100 / 2 h) | 4.63 ms | 3.52 ms | 0.80 ms | 0.68 ms | 0.70 ms | 6.6× |
| medium (100 / 1,000 / 8 h) | 7.85 ms | 0.67 ms | 5.59 ms | 1.46 ms | 0.68 ms | 11.6× |
| medium peak (100 / 1,000 / 4 h) | 4.64 ms | 3.85 ms | 1.16 ms | 0.70 ms | 0.66 ms | 7.0× |
| large (500 / 5,000 / 8 h) | 7.93 ms | 0.50 ms | **26.6 ms** | 3.04 ms | 0.50 ms | **15.9×** |
| large peak (500 / 5,000 / 4 h) | 4.55 ms | 3.81 ms | 3.28 ms | 0.80 ms | 0.63 ms | **7.2×** |

Exact courier distances computed per decision (large): full 249.6, pruned 1.0, bounded 38.5, adaptive
1.0. Large peak: full 26.5, pruned 1.0, bounded 3.7, adaptive 1.0.

### What the numbers say

- **Pruning wins when available couriers are dense.** The nearest courier sits within a small
  radius, so the search stops after a few thousand nodes instead of covering the city (140k).
- **Pruning loses its edge under peak load.** Few couriers are free (about 3–27 on average) and
  they are far away, so the search must expand nearly as far as the baseline before the first
  courier is settled. That gives only 1.2×.
- **Bounding wins under peak load.** The geodesic bound orders the few candidates well, and
  roughly 1–4 exact CCH distances settle the decision.
- **`adaptive` is never worse than the better of the two by more than noise, and is often better
  than both.** Its rule switches by expected search radius.
- **Loss: bucket-style CCH one-to-many.** Computing T(c, r) for *every* courier with one forward
  upward search each (about 400 nodes per courier on this graph) costs O(k) searches. At k = 500 that
  is 6.3× slower than one Dijkstra sweep. It only pays off when k is small. The literature's
  batched techniques (RPHAST, §5) restructure exactly this step, and were not implemented here.

## 4. Threats to validity

- **Threshold tuned in-sample.** The adaptive threshold (20,000 nodes per available courier) was
  chosen from these Tricity results. It is an engineering constant, not a fitted model. Exactness
  does not depend on it, but speed does. See §7 for the 17M-node graph as an out-of-sample check.
- **Synthetic workload.** Couriers start at uniformly random nodes, demand is uniform in time,
  restaurants are random nodes, and preparation times are U[8, 20] min. Real demand clusters
  spatially, which would favour `pruned` more often.
- **One machine, wall-clock timing.** Absolute latencies vary by about ±20% between runs on this
  laptop, and per-seed decision means differ by 5–19%. The ordering of strategies was the same in
  every seed.
- **Baseline strength.** `full` is the straightforward exact baseline, not the strongest possible
  one. A RPHAST-style one-to-many would be a stronger competitor than our `cch` variant.

## 5. Related work (verified)

- S. Knopp, P. Sanders, D. Schultes, F. Schulz, D. Wagner. *Computing Many-to-Many Shortest Paths
  Using Highway Hierarchies.* ALENEX 2007. Bucket-based many-to-many distance tables: backward
  searches store entries in per-node buckets, and forward searches scan them. Our `cch` strategy is a
  simplified one-to-many instance of this idea.
- D. Delling, A. V. Goldberg, R. F. Werneck. *Faster Batched Shortest Paths in Road Networks.*
  ATMOS 2011. RPHAST for one-to-many and many-to-many queries; the stronger baseline we did not
  implement.
- J. Dibbelt, B. Strasser, D. Wagner. *Customizable Contraction Hierarchies.* (SEA 2014 / ACM JEA.)
  The CCH used here.
- V. Buchhold, P. Sanders, D. Wagner. *Fast, Exact and Scalable Dynamic Ridesharing.* ALENEX 2021.
  An exact ride-sharing dispatcher (best insertion) built on CCH with local buckets. It is the
  closest prior art in spirit: exact dispatch decisions without exhaustive distance tables.
- M. Laupichler, P. Sanders. *Fast Many-to-Many Routing for Dynamic Taxi Sharing with Meeting Points*
  (KaRRi). ALENEX 2024. Bundled many-to-many queries with pruning for ride-sharing dispatch.
- R. K. Ahuja, K. Mehlhorn, J. B. Orlin, R. E. Tarjan. *Faster Algorithms for the Shortest Path
  Problem.* JACM 1990. The radix heap used in the priority-queue study.
- M. W. Ulmer, B. W. Thomas, A. M. Campbell, N. Woyak. *The Restaurant Meal Delivery Problem: Dynamic
  Pickup and Delivery with Deadlines and Random Ready Times.* Transportation Science 55(1), 2021.
  Meal delivery with uncertain ready times; the operational setting this dispatcher abstracts.
- D. Reyes, A. Erera, M. Savelsbergh, S. Sahasrabudhe, R. O'Neil. *The Meal Delivery Routing
  Problem.* Optimization Online working paper, 2018.

**Positioning.** Exact dispatch with bounded or pruned shortest-path work is established in
ride-sharing (Buchhold et al.; Laupichler and Sanders). The specific combination here has not, to my
knowledge, been isolated and measured in the papers listed above:
- the ETA objective with a preparation-time floor max(now + T, t_ready), which makes many couriers
  tie and strengthens both stopping rules;
- the density-adaptive switch between a pruned Dijkstra and a geodesic-bounded CCH evaluation;
- system-level exactness checks via deterministic replay hashes.

Treat that as a claim to check with a proper literature search before any publication, not as an
established novelty.

## 6. Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/dispatchlab import data/raw/tricity-261017.osm.pbf data/processed/tricity.graph --clip-tricity
./build/benchmark_eta data/processed/tricity.graph --samples 200 --seeds 3 --couriers 20,100,500 --out benchmarks/results/eta_tricity.json
./build/benchmark_eta data/processed/tricity.graph --samples 100 --seeds 3 --couriers 20,100,500 --strategies pruned,bounded,adaptive --out benchmarks/results/eta_tricity_adaptive.json
```

## 7. Scale check (out of sample): northern India, 17.2M nodes

Graph: the full Geofabrik `northern-zone` extract (17,176,411 nodes, 35,415,385 directed edges; CCH
preprocessing 16.5 s). Micro benchmark, 20 samples per cell, couriers at uniformly random nodes across
the region. Raw data: `benchmarks/results/eta_northern.json`. Every sample's choice was identical
across all five strategies (`EXACTNESS_OK`).

| Couriers (food not ready) | full | pruned | cch | bounded | adaptive | adaptive vs full |
|---:|---:|---:|---:|---:|---:|---:|
| 20 | 1,246 ms | 60.2 ms | 24.2 ms | **4.64 ms** | 4.72 ms | 264× |
| 100 | 1,257 ms | 7.90 ms | 103 ms | 3.64 ms | **3.52 ms** | 357× |
| 500 | 1,255 ms | **2.30 ms** | 523 ms | 3.03 ms | 3.16 ms | 397× |

(Medians. The food-ready regime is similar: adaptive 2.6–3.9 ms against 1.23–1.26 s for full.)

- **The effect grows with graph size.** The baseline must sweep nearly the whole region (about 17M
  nodes settled) whatever the decision needs, while the decision-aware strategies settle 3–4
  thousand nodes.
- **The Tricity-tuned threshold transfers imperfectly.** At 17M nodes, n / k ≥ 20,000 for every
  k ≤ 858, so `adaptive` always chose `bounded`. At k = 500, `pruned` would have been 1.3× faster.
  `adaptive` was within 1.4× of the best exact strategy in every cell, but the rule should be
  expressed in a graph-size-independent quantity (for example the expected geodesic distance to the
  nearest available courier) rather than raw node counts. That is future work.
- **p95 is high for every strategy alike** (about 0.2–0.7 s). It comes from the shared T(r, d)
  A* query when the uniform sampler, failing to find a customer within 6 km in sparse regions, falls
  back to a distant node. It is a property of the synthetic sampler on this region, not of the
  strategies.
- **The bucket-style CCH loss persists at scale** (523 ms at 500 couriers): one forward upward
  search per courier.
