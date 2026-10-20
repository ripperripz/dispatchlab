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
* Full experiment with the PRD's large size (500 couriers / 5,000 orders) at normal and peak load:
  `experiments/results/full/` (180 runs, fairness check OK, 451 s).
* **CCH (P2), passed the kill-switch:** own geometric nested-dissection order (no METIS dependency),
  chordal contraction, basic triangle customization, pruned upward bidirectional query, iterative
  unpacking. Exact on fixtures, random graphs, re-customized traffic weights and 10,000 Tricity
  queries (ctest `routing_agreement_tricity_cch`, verify AC-17). On Tricity: about 160 ms
  preprocessing, 6.4× fill-in, 29 MiB, median query about 12× faster than Dijkstra (PERFORMANCE.md).
  Because CCH succeeded, the replacement graph-layout / priority-queue study was not needed.

## Console (frontend), branch `feat/frontend-console`

* `dispatchlab serve <graph>`: loopback-only HTTP/1.1 server (POSIX sockets, thread per connection)
  serving `viewer/` plus a JSON API computed live by the engine (route with settle-order traces for
  all four algorithms, simulate, compare, agreement, determinism, selftest, results).
* Console views: Overview, Routing lab (signature: four search spaces animating in settle order as
  small multiples), Simulation (live runs with verified replay), Policies (head-to-head on one shared
  scenario + experiment explorer), Benchmarks, Correctness (live checks). Vercel/Cloudflare/Linear
  register (user-pinned), self-hosted Geist, light/dark, responsive to 390px.
* Offline `file://` mode keeps working (replay + results bundles from `export-viewer`).
* verify.sh: new CONSOLE check starts the server, checks route agreement against the CLI, checks that
  a live simulation reproduces the CLI's state hash, checks selftest and path-traversal rejection.
  The AC-13 network check now scans every viewer file for external hosts and forbids network APIs
  other than `fetch` in `api.js` with relative `/api/` paths.
* Impeccable finish review: ship after two fix rounds. DESIGN.md + `.impeccable/design.json` record
  the system.

## Phase 2: scale, performance, research (branch `feat/scale-perf-research`)

* A* chord heuristic (admissible, one sqrt on shared float unit vectors).
* Priority-queue study: Dijkstra templated on the heap; 4-ary, radix and std::priority_queue
  variants, all tested against references; radix heap 1.39× (Tricity) / 1.51× (17M nodes) faster.
* Graph-layout study (`relabel`, six orders) with per-layout hardware counters at 17M nodes:
  identical instructions, IPC 0.51 (random) to 1.44 (DFS).
* Server: worker pool with bounded queue (503 shedding); CCH split into a shared immutable hierarchy
  plus per-thread workspaces (lock-free); `/api/distance`; `benchmark_server`: about 20k vs 5k req/s.
* Scale: northern-zone extract imported (17,176,411 nodes, 35,415,385 edges, 9.8 s, 4.1 GB peak);
  1,000/1,000 agreement queries pass (Dijkstra, Bidirectional, A*).
* Research (docs/RESEARCH.md): exact decision-aware Greedy ETA strategies (pruned, cch, bounded,
  adaptive); identical choices and hashes everywhere; adaptive 7–16× lower decision time. The
  bucket-style CCH one-to-many is a measured loss at many couriers. verify.sh has a RESEARCH check.

## Research v2/v3 + Linux (branch `feat/research-v2`)

* Linux: Ubuntu 24.04 / GCC 13 reference image (`ci/Dockerfile`) and GitHub Actions workflow. The
  first Linux build failed on GCC `-Wshadow` (enumerators shadowing the `Dijkstra`/`AStar` types);
  fixed by renaming them to `Algorithm::kDijkstra` etc.
* RPHAST-style restricted CCH many-to-one (`CCH::distances_to`) as the strong exact baseline.
* Strategies `hybrid` (budgeted pruned → bounded; negative result) and `auto` (pruned/RPHAST; best on
  Tricity, 2–7× worse than `adaptive` at 17M nodes).
* Clustered, time-of-day demand mode; uniform scenario hashes unchanged.
* A benchmark run was invalidated by laptop sleep; repeated under `caffeinate`.

## Hosted demo (2026-10-20)

* `dispatchlab export-demo` records real console API responses in-process (41 recordings: 8 routes ×
  3 traffic levels with search traces, 6 simulations, 3 comparisons, agreement, determinism,
  self-test). `scripts/build-demo.sh` packages them with the viewer into `dist/demo/`.
* `viewer/api.js` gains a `demo` mode that replays recordings keyed by path and parameters and refuses
  anything unrecorded. Unrecorded inputs are locked, and a "Recorded demo" badge is always visible.
* Published as a private claude.ai artifact for a classroom demo. Tested locally under headless Chrome
  (all six views). Live, offline and AC-13 behaviour is unchanged.

## In progress

* Nothing in progress. All mandatory AC and the optional AC-17 (CCH) pass in `verify.sh` (exit 0,
  about 4m50s on Apple M4).

## Acceptance criteria (PRD §17)

Status as of the last full `./scripts/verify.sh` run (2026-10-19, clean Release build, exit 0). Each
row lists the check that backs it.

| AC | Criterion | Status |
|---|---|---|
| AC-01 | Tricity OSM data imported | PASS: import of committed extract (>100k nodes) + `osm import:*` fixture tests |
| AC-02 | Directed CSR graph correct | PASS: `dispatchlab info` (validate, SCC=1, id mapping) + `graph:*` tests |
| AC-03 | Binary heap passes tests | PASS: `heap:*` (randomized vs std::priority_queue, invariant checks) |
| AC-04 | Iterative SCC works | PASS: `scc:*` (CLRS fixture, 1M-node path/cycle, brute-force oracle) |
| AC-05 | Dijkstra exact | PASS: `dijkstra:*` + Floyd–Warshall oracle on random graphs |
| AC-06 | A* agrees with Dijkstra | PASS: 10k agreement + `astar:*` (admissibility, per-edge bound) |
| AC-07 | Bidirectional agrees with Dijkstra | PASS: 10k agreement + `bidirectional:*` |
| AC-08 | ≥ 10,000 agreement tests pass | PASS: `routing_agreement --queries 10000` (costs equal, path sums == cost) |
| AC-09 | Simulator processes delivery events | PASS: 7 event types, `check-log` invariants, 5/5 corrupted logs rejected, `simulator:*`, `replay:*`, `event queue:*` |
| AC-10 | Nearest policy | PASS: `simulate --policy nearest` + `dispatch: nearest*` |
| AC-11 | Greedy ETA policy | PASS: `simulate --policy greedy_eta` + `dispatch: greedy eta*` |
| AC-12 | Identical inputs → identical hashes | PASS: two runs → byte-identical logs + equal FNV-1a hash; replay hash equal; different seed → different hash |
| AC-13 | Local viewer shows a recorded simulation | PASS: `export-viewer`, no network refs, `validate-replay` (re-hash), Node self-test of viewer core |
| AC-14 | Routing latency and memory benchmarks | PASS: benchmark_graph/routing/dispatch → PERFORMANCE.md with environment fields |
| AC-15 | Policies compared fairly | PASS: experiment (4 sizes × 3 traffic × 5 paired seeds) + `check-experiment` (same scenario hash per cell) |
| AC-16 | Builds with documented instructions | PASS: README quick start executed verbatim in a clean copy; subcommands and paths cross-checked |
| AC-17 | CCH exact (optional) | PASS: `cch:*` tests + 10,000-query agreement with `--cch` |

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
