# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

delegated: plain HTML, CSS and JavaScript (classic scripts, no build step, no framework). Reason: the PRD
requires a framework-free local viewer that also opens from `file://` (acceptance criterion AC-13), and the
map/search visualizations are canvas work that gains nothing from a framework. The C++ binary serves the
same files over a localhost-only HTTP server (`dispatchlab serve`) for live mode.

## Users

All three are primary (confirmed):

- **Course evaluators** (ADSA graders, live demo/presentation): need to see that the algorithms are
  correct and how they differ, on real data, without reading code.
- **Interviewers** (FAANG/HFT-style portfolio walkthroughs): judge performance engineering and
  judgment: measured latency, memory, trade-offs, honest losses.
- **The developer** (the author, experimenting): runs scenarios, compares policies, checks results
  while working.

## Product Purpose

DispatchLab is a deterministic food-delivery routing and dispatch simulator in C++20 on the real
OpenStreetMap road network of Chandigarh, Mohali and Panchkula. The frontend makes the engine
inspectable: replay recorded simulations, run shortest-path queries live and compare algorithms, compare
dispatch policies on identical scenarios, and read benchmark results. Success: someone can run the
binary, open the UI, and understand and interrogate every claim the project makes in minutes.

## Positioning

Everything is computed by the project's own engine on a real 142k-node road graph: its own CSR graph,
heap, Dijkstra / Bidirectional / A* / CCH, discrete-event simulator and policies. The UI is a window onto
exact, reproducible computation (state hashes, 10,000-query agreement), not onto a map API or mock data.

## Operating Context

- Runs on the user's machine: `./build/dispatchlab serve` (localhost only) for live mode, or
  `viewer/index.html` opened from disk for recorded replays. No deployed backend and no third-party APIs
  or map tiles at runtime.
- Used in a projected classroom demo, a screen-shared interview, and day-to-day development.
- Inputs and outputs are files the CLI already produces: `tricity.graph`, scenario JSON, JSON Lines event
  logs, viewer replay JSON, `experiments/results/*.json|csv|md`, `benchmarks/results/*.json`.

## Capabilities and Constraints

- Engine capabilities: import, route (dijkstra, bidirectional, astar, CCH), gen-scenario, simulate
  (nearest, greedy_eta; free/moderate/heavy traffic), replay with FNV-1a state hash, invariant checks,
  experiments, benchmarks.
- Times are integer milliseconds; costs are travel time; policies' assignment latency is wall-clock and
  non-deterministic (excluded from hashes).
- Traffic is synthetic and must be labelled as such. Speeds are documented class defaults, not measured.
- Requested views: simulation replay, routing lab, policy comparison, benchmarks, "and more if
  possible".
- Must keep `scripts/verify.sh` passing (AC-13 checks the viewer: no network references, replay JSON
  loads from `file://`).

## Brand Commitments

- Name: DispatchLab. No logo existed before the frontend build.
- User-pinned visual references (binding): the frontend should sit alongside **Vercel's dashboard,
  Cloudflare's dashboard and Linear** at their craft level: "just beautiful". Convention played
  straight at full fidelity is the commitment, not a quirky reinterpretation.

## Evidence on Hand

- Real measured results: `PERFORMANCE.md`, `benchmarks/results/{graph,routing,dispatch}.json`,
  `experiments/results/policy_comparison.*`, `experiments/results/full/*`.
- Real road data: `data/raw/tricity-261017.osm.pbf` (© OpenStreetMap contributors, ODbL).
- No users, testimonials or deployment exist; never invent them. Benchmark numbers are only ever shown
  from those files or computed live.

## Product Principles

1. Show the computation, not a picture of it: every number on screen comes from the engine or its files.
2. Correctness is visible: agreement between algorithms, invariants and hashes are first-class data.
3. Honest comparisons: losses and ties are shown as plainly as wins.
4. Works offline and locally, always.
5. Fast to a first insight: one click from opening to a meaningful view.

## Accessibility & Inclusion

No product-specific standard was established; the WCAG AA floor applies (readable on a projector,
keyboard operable, colour never the only carrier of meaning).
