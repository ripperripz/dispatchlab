# CLAUDE.md: DispatchLab

C++20 food-delivery routing and dispatch simulator on the Tricity OSM road network. The spec is
`docs/PRD.md`; status is in `PROGRESS.md`.

## Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j   # build (-Werror on project code)
ctest --test-dir build --output-on-failure                                 # unit tests + Tricity import + 10k agreement
./build/dispatchlab_tests -tc="simulator:*"                                # one doctest group
./scripts/verify.sh                                                        # full acceptance gate (AC-01..AC-16)
```

## Layout

* `include/dispatchlab/`: headers. `graph.hpp` (CSR + reverse CSR, serialization), `binary_heap.hpp`,
  `scc.hpp`, `routing.hpp` (Dijkstra template over accessors, Bidirectional, A*, Router),
  `simulator.hpp` (events, EventQueue, StateMachine, Simulator, replay), `dispatch.hpp`,
  `scenario.hpp`, `traffic.hpp`, `experiment.hpp`, `json.hpp`, `hash.hpp` (FNV-1a), `rng.hpp`
  (xoshiro256**)
* `src/…`: implementations mirror the headers. `src/main.cpp` is the CLI.
* `tests/`: doctest. Test-case name prefixes (`heap:`, `graph:`, `scc:`, `dijkstra:`, `astar:`,
  `bidirectional:`, `routing:`, `simulator:`, `event queue:`, `dispatch: nearest`,
  `dispatch: greedy eta`, `replay:`, `osm import:`) are used as filters by `scripts/verify.sh`. Keep
  them.
* `benchmarks/`: three benchmark binaries writing JSON to `benchmarks/results/`.
* `viewer/`: framework-free. `core.js` is pure (tested by `selftest.mjs` under Node); `app.js` is the DOM.

## Conventions and invariants

* Determinism: no `std::hash`, no `std::*_distribution`, no wall clock in logs or hashes. Use `Rng`,
  `Fnv1a64` and integer ms times. Heap ties are broken by node id or event id.
* Every simulator state transition goes through `StateMachine::apply`, which replay also uses. To add
  state, extend `state_hash()` and make sure it is derivable from the log.
* Edge weights are `ceil(len·3600/speed)` ms, and `GraphMeta::max_speed_kmh` must bound every class
  speed (A* admissibility). Traffic may only scale weights up.
* Never weaken `scripts/verify.sh`, reduce the 10,000-query suite, or skip/expected-fail tests.
* Benchmark numbers in `PERFORMANCE.md` are generated. Only edit the hand-written analysis outside the
  `BEGIN/END GENERATED` markers, and only with claims backed by the generated tables.
* Viewer code is plain JavaScript (the PRD requires HTML/CSS/JS with no build step), not TypeScript.
