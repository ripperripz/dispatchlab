# PROGRESS

Last updated: 2026-10-19. Re-read this file, then `docs/PRD.md`, before resuming work.

## Done

* Repo scaffold per PRD §12.2. CMake builds Release with C++20 and `-Wall -Wextra -Wpedantic -Wshadow
  -Werror` on project code. doctest 2.4.11 is vendored, so builds work offline.
* Data: Geofabrik `asia/india/northern-zone` (2026-10-17T20:20:51Z, MD5 verified), clipped with
  osmium-tool to bbox 76.62,30.62,76.95,30.80 → `data/raw/tricity-261017.osm.pbf`, committed
  (`docs/DATA.md`, `scripts/fetch_data.sh`).
* Graph: libosmium importer (drivable filter, one-way, roundabouts, access rules, bbox clip, haversine,
  class speeds, ceil-ms weights, compact ids by ascending OSM id). Iterative Tarjan SCC keeps the
  largest component. CSR + reverse CSR, validation, checksummed DLGRAPH1 serialization, adjacency-list
  baseline. Tricity: 142,425 nodes and 298,112 directed edges; v_max = 60 km/h.
* Custom binary heap (hole sift, lazy deletion), with randomized tests against `std::priority_queue`.
* Routing: Dijkstra (template over CSR, reverse CSR and adjacency accessors), Bidirectional Dijkstra
  (`top_f + top_b ≥ mu`), A* (floor-geodesic / v_max, with re-opening). Tested against a Floyd–Warshall
  oracle; heuristic admissibility test; 10,000-query Tricity agreement suite (ctest + verify).
* Simulator: integer-ms DES with a `(timestamp, priority, id)` queue on the custom heap, all seven
  event types, FIFO queueing, JSONL log, FNV-1a canonical state hash. `StateMachine` enforces PRD §13.3
  on every transition and is shared with replay. Replay takes no graph/router/policy, and instrumented
  counters show zero routing/dispatch calls. A corrupted-log self-test covers 5 invariant classes.
* Dispatch: Nearest (haversine, id tie-break); Greedy ETA (one reverse Dijkstra per decision for all
  T(c,r), A* for T(r,d), ties broken by (ETA, T(c,r), id)).
* Traffic: free/moderate/heavy per-edge multipliers ≥ 1 (documented in `docs/DATA.md`).
* Experiment runner: sizes × traffic × paired seeds, one scenario per cell shared by both policies,
  parallel and deterministic. Writes CSV/JSON (reproducibility metadata) and Markdown (paired diffs,
  win counts). Includes a fairness checker.
* Benchmarks: CSR vs adjacency (memory, load, build, traversal, Dijkstra), routing
  (median/p95/p99/settled/memory, distance buckets), dispatch latency vs courier count. `perf-report`
  regenerates PERFORMANCE.md.
* Viewer: offline canvas (file:// via `replay_data.js`, or open/drop a JSON), play/pause/replay/speed/
  scrub, timeline, metrics. `selftest.mjs` checks the viewer logic under Node. Checked visually in
  headless Chrome from file://.
* README quick start (executed verbatim by verify.sh), CLAUDE.md, plan.md.

## In progress

* Waiting on the first full `verify.sh` run; this file will be updated with the result.

## Acceptance criteria (PRD §17)

Status as of the last full `./scripts/verify.sh` run. See below.

| AC | Criterion | Status |
|---|---|---|
| AC-01 | Tricity OSM data imported | pending verify |
| AC-02 | Directed CSR graph correct | pending verify |
| AC-03 | Binary heap passes tests | pending verify |
| AC-04 | Iterative SCC works | pending verify |
| AC-05 | Dijkstra exact | pending verify |
| AC-06 | A* agrees with Dijkstra | pending verify |
| AC-07 | Bidirectional agrees with Dijkstra | pending verify |
| AC-08 | ≥ 10,000 agreement tests pass | pending verify |
| AC-09 | Simulator processes delivery events | pending verify |
| AC-10 | Nearest policy | pending verify |
| AC-11 | Greedy ETA policy | pending verify |
| AC-12 | Identical inputs → identical hashes | pending verify |
| AC-13 | Local viewer shows a recorded simulation | pending verify |
| AC-14 | Routing latency and memory benchmarks | pending verify |
| AC-15 | Policies compared fairly | pending verify |
| AC-16 | Builds with documented instructions | pending verify |
| AC-17 | CCH exact (optional) | not started |

## Known issues / notes

* Platform: developed and verified on macOS 27 (Apple M4, AppleClang 21, Homebrew libosmium 2.23.1).
  The PRD's reference platform is Linux/WSL2. The code uses only POSIX and standard C++, and CMake
  searches the usual include paths, but no Linux build has been run in this environment.
* Viewer position interpolation along a leg is linear in geometric distance. The simulator fixes only
  departure and arrival times, so this is a display approximation.
* Assignment latency is wall-clock and is the only experiment column that differs between identical
  runs. It is excluded from logs and hashes.
* Graph Dijkstra on the full graph settles about 69k nodes per median query. No speed-up techniques
  (beyond A*/bidirectional) are in the mandatory scope.

## BLOCKED

Nothing.
