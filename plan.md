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
