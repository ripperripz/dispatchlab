# DispatchLab

DispatchLab is a deterministic, performance-oriented food-delivery routing and dispatch simulator,
written in C++20 and run on the real OpenStreetMap road network of Chandigarh, Mohali and Panchkula.
It ships its own CSR graph, binary heap, iterative SCC, three exact shortest-path algorithms
(Dijkstra, Bidirectional Dijkstra, A*), plus the optional Customizable Contraction Hierarchies
(CCH) stretch goal, and a discrete-event simulator with two dispatch policies
(Nearest, Greedy ETA). It also includes event-log replay with stable state hashes, a reproducible
experiment runner, benchmarks and an offline web viewer. The spec is in [`docs/PRD.md`](docs/PRD.md).

* Data and model details (bbox, speeds, traffic multipliers, file formats): [`docs/DATA.md`](docs/DATA.md)
* Measured performance and policy results: [`PERFORMANCE.md`](PERFORMANCE.md)
* Status and acceptance criteria: [`PROGRESS.md`](PROGRESS.md)

## Requirements

* CMake ≥ 3.20 and a C++20 compiler (GCC ≥ 11 or Clang ≥ 14). Linux/WSL2 is the reference
  environment; macOS works too.
* libosmium + protozero headers, zlib, bzip2 and expat, for the OSM importer.
  * Debian/Ubuntu/WSL2: `sudo apt install build-essential cmake libosmium2-dev libprotozero-dev zlib1g-dev libbz2-dev libexpat1-dev`
  * macOS (Homebrew): `brew install cmake libosmium`
* Optional: Node.js, which verify.sh uses to run the viewer self-test; osmium-tool, to re-clip the raw data.

Nothing contacts the network at build or run time. The test framework (doctest 2.4.11) is vendored
in `third_party/`. The clipped Tricity extract is committed at `data/raw/tricity-261017.osm.pbf`.

## Quick start

These commands are run verbatim by `./scripts/verify.sh` on a clean copy of the repo:

<!-- verify:quickstart:begin -->
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/dispatchlab import data/raw/tricity-261017.osm.pbf data/processed/tricity.graph --clip-tricity
./build/dispatchlab info data/processed/tricity.graph
./build/dispatchlab gen-scenario data/processed/tricity.graph data/scenarios/demo.json --couriers 40 --orders 300 --seed 42 --traffic moderate
./build/dispatchlab simulate data/processed/tricity.graph data/scenarios/demo.json --policy greedy_eta --log data/scenarios/demo_events.jsonl
./build/dispatchlab replay data/scenarios/demo.json data/scenarios/demo_events.jsonl
./build/dispatchlab export-viewer data/processed/tricity.graph data/scenarios/demo.json data/scenarios/demo_events.jsonl --policy greedy_eta --out viewer/replay.json --js viewer/replay_data.js --results-js viewer/results_data.js
```
<!-- verify:quickstart:end -->

Then start the console and open http://127.0.0.1:8080:

```bash
./build/dispatchlab serve data/processed/tricity.graph
```

Without a server, `viewer/index.html` also opens directly from disk (double-click it). It shows the
exported replay plus the benchmark and experiment results; the live features ask you to start
`serve`.

## Full verification

```bash
./scripts/verify.sh
```

This does a clean Release build with `-Wall -Wextra -Wpedantic -Werror` and then:

* runs the unit tests (ctest)
* imports Tricity and checks the 10,000-query routing agreement
* checks simulation determinism (two runs give byte-identical logs and equal hashes) and replay from
  the log
* checks the invariants, including negative tests on corrupted logs
* runs the paired policy experiment and the benchmarks, and regenerates `PERFORMANCE.md`
* builds and validates the viewer replay
* runs the README quick start above in a clean copy

It prints PASS/FAIL per acceptance criterion and exits 0 only if everything passes. It takes about
5 minutes on an Apple M4.

## Command reference

Run `./build/dispatchlab help` for all options.

| Command | Purpose |
|---|---|
| `dispatchlab import <pbf> <out.graph> --clip-tricity` | libosmium import: drivable roads, one-way, haversine lengths, class speeds, largest SCC, CSR binary |
| `dispatchlab info <graph>` | statistics, integrity check, strong connectivity |
| `dispatchlab route <graph> <src> <dst> --algo astar` | one shortest-path query (`dijkstra`, `bidirectional`, `astar`) |
| `dispatchlab gen-scenario <graph> <out.json> ...` | generate a seeded workload once (couriers, restaurants, orders, prep times, traffic) |
| `dispatchlab simulate <graph> <scenario> --policy nearest\|greedy_eta --log events.jsonl` | deterministic simulation; prints metrics and `final_state_hash` |
| `dispatchlab replay <scenario> <events.jsonl>` | rebuild final state from the log alone (no routing/dispatch) |
| `dispatchlab check-log <scenario> <events.jsonl> [--self-test-corruptions]` | invariant check; proves corrupted logs are rejected |
| `dispatchlab experiment <graph> --config experiments/configs/verify.json --out experiments/results` | paired policy comparison across sizes × traffic × seeds |
| `dispatchlab check-experiment experiments/results/policy_comparison.csv` | fairness check (same scenario hash per cell) |
| `dispatchlab export-viewer <graph> <scenario> <events.jsonl> --out viewer/replay.json --js viewer/replay_data.js` | viewer replay document |
| `dispatchlab validate-replay viewer/replay.json` | validate a replay and re-check its hash |
| `dispatchlab serve <graph> --port 8080` | the console: live frontend + JSON API on 127.0.0.1 |
| `dispatchlab perf-report ...` | regenerate the measured sections of PERFORMANCE.md |

Benchmarks are separate binaries: `./build/benchmark_graph`, `./build/benchmark_routing` and
`./build/benchmark_dispatch`, each taking `<graph> --out file.json`. The routing agreement suite is
`./build/routing_agreement <graph> --queries 10000 --seed 20261018`. Add `--cch` to include CCH in
the comparison.

## Running experiments

```bash
./build/dispatchlab experiment data/processed/tricity.graph --config experiments/configs/verify.json --out experiments/results
./build/dispatchlab experiment data/processed/tricity.graph --config experiments/configs/full.json --out experiments/results/full
```

`verify.json` covers the small and medium sizes; `full.json` adds the PRD's large size (500 couriers,
5,000 orders). Each size runs at normal load and at peak load (half the order window), under all three
traffic scenarios and five paired seeds. Results land in `policy_comparison.csv`/`.json`, with
reproducibility metadata (dataset, seeds, compiler, hardware, parameters), and in
`policy_summary.md` (paired differences and win counts).

## Performance study, scale, research

* **Performance engineering** (PERFORMANCE.md, phase 2): chord-bound A* (1.45× lower median latency
  than haversine A*), a priority-queue study (radix heap 1.39–1.51× faster than a binary heap; a
  4-ary heap gives no gain), a graph-layout study with hardware counters (identical instruction
  counts across six node orders; IPC 0.51 for random vs 1.44 for DFS order at 17M nodes), and a
  worker-pool server (4× the throughput and 6.4× lower p99 than thread-per-connection).
* **Scale:** the full Geofabrik northern-zone extract (17.2M nodes, 35.4M edges) imports in 10 s
  and passes the 1,000-query agreement suite.
* **Research** ([docs/RESEARCH.md](docs/RESEARCH.md)): decision-aware Greedy ETA. Exact strategies
  (`greedy_eta_pruned`, `greedy_eta_bounded`, `greedy_eta_adaptive`) make provably identical
  decisions with up to 16× less decision time; identical simulation hashes are checked by verify.sh.

```bash
./build/benchmark_layout data/processed/tricity.graph --queries 1000 --out benchmarks/results/layout_tricity.json
./build/benchmark_server data/processed/tricity.graph --seconds 4 --out benchmarks/results/server.json
./build/benchmark_eta data/processed/tricity.graph --samples 200 --seeds 3 --out benchmarks/results/eta_tricity.json
./build/dispatchlab import data/raw/northern-zone-261017.osm.pbf data/processed/northern.graph
```

The last command needs the full extract (`./scripts/fetch_data.sh` downloads it; 224 MB) and about
4 GB of RAM. Every Greedy ETA strategy is also a `--policy` value for `simulate` and `experiment`.

## Console (frontend)

`dispatchlab serve <graph> [--port 8080]` runs a small HTTP server built into the binary (POSIX
sockets, loopback interface only, no extra dependencies). It serves the console in `viewer/` and a
JSON API computed live by the engine:

| View | What it does | API |
|---|---|---|
| Overview | dataset, engine facts (CCH, validation), headline benchmark and policy results | `/api/info`, `/api/network`, `/api/results` |
| Routing lab | click two points (or press R). All four algorithms run on the engine; compare cost, nodes settled and latency, and watch their search spaces unfold in settle order side by side | `/api/route` |
| Simulation | configure couriers, orders, seed, traffic and policy; run on the engine; play back couriers and orders with a timeline and event log; the replay is verified against the final state hash | `/api/simulate` |
| Policies | Nearest vs Greedy ETA on one shared scenario (metrics, deliveries over time, delivery-time distribution), plus a paired-seed explorer over the experiment results | `/api/compare`, `/api/results` |
| Benchmarks | latency percentiles, search effort, trip-length buckets, CCH preprocessing, CSR vs adjacency, dispatch decision latency | `/api/results` |
| Correctness | live algorithm agreement, determinism with replay, rejection of corrupted logs | `/api/agreement`, `/api/determinism`, `/api/selftest` |

The console is plain HTML, CSS and JavaScript with no framework and no build step. Fonts (Geist, OFL)
are self-hosted in `viewer/fonts/`, and it loads nothing from any other host. It follows the system
light/dark theme, with a manual toggle. The console's simulation hash is identical to the CLI's for
the same parameters, and `scripts/verify.sh` checks that.

**Offline:** opened from `file://`, the console reads `replay_data.js` and `results_data.js` (written
by `export-viewer --js ... --results-js ...`). Overview, Simulation (the recorded replay), Policies
results and Benchmarks work; the live-only parts explain how to start the server.

## Repository layout

```text
include/dispatchlab/  public headers (graph, binary_heap, routing, simulator, dispatch, ...)
src/graph/            CSR graph + serialization, iterative SCC, libosmium importer
src/routing/          Dijkstra, bidirectional Dijkstra, A*, CCH (P2)
src/simulation/       event queue, scenario generator, traffic, simulator/state machine, replay, log I/O
src/dispatch/         Nearest and Greedy ETA policies
src/experiments/      experiment runner
src/tools/            viewer export, PERFORMANCE.md generator
src/main.cpp          CLI
tests/                doctest unit tests, fixtures, routing_agreement.cpp
benchmarks/           graph / routing / dispatch benchmarks (+ results/)
experiments/          configs/, results/
src/server/           loopback HTTP server + console JSON API
viewer/               console frontend (views/, fonts/, offline bundles)
data/raw/             clipped OSM extract (ODbL)
scripts/              verify.sh, fetch_data.sh
```

## License and attribution

Road data © OpenStreetMap contributors, available under the Open Database License (ODbL 1.0).
doctest is MIT-licensed.
