# DispatchLab build plan

Order follows PRD §3 priorities (P0 → P1 → P2). Each step ends with a commit.

1. Scaffold: CMake (Release, C++20, -Wall -Wextra -Werror), vendored doctest, repo layout per PRD §12.2,
   `scripts/verify.sh` covering AC-01..AC-16.
2. Graph core on hand-built fixtures: CSR + reverse CSR, validation, binary serialization (checksummed),
   adjacency-list baseline, iterative Tarjan SCC.
3. OSM import (libosmium): Geofabrik northern-zone → osmium clip to Tricity bbox → drivable filter,
   one-way, haversine lengths, class speeds, ms weights, compact ids, largest SCC, `tricity.graph`.
4. Custom binary heap + randomized tests against std::priority_queue.
5. Dijkstra (reference), Bidirectional (Goldberg stopping rule), A* (admissible geodesic heuristic,
   v_max = fastest class); Floyd–Warshall oracle tests; 10,000-query Tricity agreement suite.
6. Deterministic DES: integer ms, (time, priority, id) queue, seven event types, FIFO order queue,
   JSONL event log, FNV-1a canonical state hash, StateMachine-enforced invariants, replay.
7. Nearest and Greedy ETA policies; synthetic traffic (free / moderate / heavy).
8. Experiment runner (paired seeds, shared scenarios), benchmarks, PERFORMANCE.md generator.
9. Offline canvas viewer (play/pause/replay/speed/scrub, timeline, metrics) + Node self-test.
10. README whose quick start verify.sh executes verbatim.
11. P2 (only after verify passes): CCH, time-boxed; otherwise the graph-layout / priority-queue study.

---

# Phase 2: scale, performance engineering, research (branch `feat/scale-perf-research`)

Goal: raise DispatchLab from course project to a defensible systems/research portfolio piece.
Rules carry over: correctness first, every claim backed by a measurement, losses reported, verify.sh
stays green, no hardcoded results.

## A. Performance engineering (low-latency C++)
1. **A* heuristic cost.** Replace per-push haversine (sin/cos/asin) with the chord bound: unit-sphere
   xyz precomputed per node, h = floor(R·|p_v − p_t| / v_max). Chord ≤ arc ≤ any path length, so h
   stays admissible (and consistent: Euclidean triangle inequality). Test admissibility; benchmark
   median/p95/p99 before vs after on the same queries.
2. **Priority queues.** Make the heap a template parameter of Dijkstra; add a 4-ary heap and a radix
   heap (monotone integer keys). Randomized tests vs std::priority_queue; identical costs on the
   agreement suite; benchmark Dijkstra latency per heap.
3. **Graph layout.** Node permutations: OSM-id order (baseline), random (control), BFS, DFS, Hilbert
   curve, nested-dissection order. Re-layout CSR + coordinates, re-run the same queries (mapped ids),
   report latency and a locality proxy (mean |u−v| over edges, fraction of edges within 16 ids).
   Hardware counters need Linux `perf`; on macOS this is recorded as a limitation, not faked.
4. **Server concurrency.** Replace thread-per-connection with a fixed worker pool and bounded queue;
   add a load generator (N concurrent HTTP clients issuing /api/route) reporting throughput and
   latency percentiles; compare against the old design.

## B. Scale
5. Import the full Geofabrik northern-zone extract (Punjab, Haryana, Delhi, HP, J&K, …). Record
   import time and peak RSS, graph size, CCH preprocessing time/memory (time-boxed; report if
   infeasible), routing agreement (≥1,000 queries incl. CCH) and routing benchmarks at scale.

## C. Research: decision-aware Greedy ETA dispatch
6. Question: can a Greedy ETA dispatcher avoid most exact shortest-path work while provably making
   the same decision? Variants, all required to return the identical courier:
   - `full`: reverse Dijkstra until every available courier is settled (current baseline)
   - `pruned`: stop the reverse search once no unsettled courier can beat the best ETA
     (lower bound max(now+d, ready)+T(r,d) from the frontier key d, with the tie rule)
   - `cch`: CCH backward upward search from the restaurant + forward upward search per courier
   - `bounded`: order couriers by an admissible geodesic lower bound, compute exact CCH distances
     lazily, stop when the bound proves no remaining courier can win
   Exactness: unit tests (same choice on randomized requests) and system-level proof (identical
   final state hashes across every experiment cell). Metrics: decision latency, nodes settled,
   exact distance computations avoided, across courier counts, load and traffic.
7. Write docs/RESEARCH.md: question, method, exactness argument, results, threats to validity,
   related work (verified references only).

## D. Close
8. PERFORMANCE.md (hand-written sections for A/B), README, PROGRESS, verify.sh additions (exactness
   of dispatch variants), final clean verify.
