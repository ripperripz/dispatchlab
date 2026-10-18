# DispatchLab
## Product Requirements Document (PRD)
**Version:** 1.0 — Consolidated specification  
**Date:** October 18, 2026  
**Project Type:** Algorithms, Systems Engineering & Simulation  
**Language:** C++20  
**Developer:** Solo  
**Primary Deliverable:** ADSA course project  
**Secondary Deliverable:** FAANG/HFT-oriented engineering portfolio project  
**Research Paper:** Optional, not required for submission

| Parameter | Specification |
|---|---|
| Project name | DispatchLab |
| Domain | Food-delivery routing and dispatch optimization |
| Geographic coverage | Chandigarh, Mohali, Panchkula |
| Road network | OpenStreetMap |
| Core implementation | C++20 |
| Visualization | HTML, CSS, JavaScript |
| Data structures | CSR graph, binary heap, hash maps, event priority queue |
| Routing algorithms | Dijkstra, Bidirectional Dijkstra, A* |
| Advanced algorithm | Customizable Contraction Hierarchies (optional) |
| Simulation | Deterministic discrete-event simulation |
| Prototype target | October 19–23, 2026 |
| Final submission | November 25, 2026 |
| Development budget | Approximately 12 hours/week |

---

# 1. Executive Summary

### 1.1 What is DispatchLab?

DispatchLab is a **high-performance food-delivery routing and dispatch simulation system** built around real road-network data.

It simulates how platforms such as Swiggy, Zomato, and DoorDash assign delivery partners to orders, calculate routes, and manage deliveries.

The system answers three engineering questions:

1. How quickly and accurately can we calculate routes across a real road network?
2. How do different courier-dispatch strategies affect delivery performance?
3. What engineering techniques make routing and dispatch computation faster without sacrificing correctness?

Rather than relying on external routing APIs, DispatchLab implements its own graph representation, algorithms, and simulation infrastructure.

### 1.2 Example

Suppose five customers place food orders at different restaurants in Chandigarh.

There are 20 delivery partners distributed across Chandigarh, Mohali, and Panchkula.

DispatchLab must:

1. Represent the road network as a graph.
2. Identify which delivery partners are available.
3. Calculate travel times to restaurants.
4. Assign delivery partners to orders.
5. Simulate restaurant preparation and courier movement.
6. Record pickup and delivery events.
7. Calculate operational metrics.
8. Compare dispatch policies under identical conditions.

The same scenario can be simulated multiple times using different assignment policies.

The objective is not necessarily to produce the globally optimal dispatch schedule. The objective is to create a **correct, reproducible platform for implementing, measuring, and comparing routing and dispatch algorithms**.

---

# 2. Product Objectives

## 2.1 Primary objective

Build a working C++20 system capable of importing real road data, calculating shortest paths, and simulating food-delivery operations deterministically.

The final submission must demonstrate substantial implementation of algorithms and data structures rather than simply connecting existing services.

## 2.2 Secondary objectives

**Performance engineering**

Measure the impact of graph representation, priority queues, and routing algorithms on computation time and memory consumption.

**Systems engineering**

Demonstrate deterministic execution, reproducible experiments, structured logging, automated correctness tests, and benchmarking.

**Portfolio quality**

Produce a project that can be discussed in interviews for:

- Software engineering internships
- Systems and infrastructure engineering
- Performance-oriented engineering
- Quantitative developer roles

**Research exploration**

Investigate advanced routing optimizations when the core system is stable.

Research novelty is not a condition for project success.

---

# 3. Scope and Prioritization

The project has three priority levels.

| Priority | Component | Requirement |
|---|---|---|
| P0 | OSM road-network import | Mandatory |
| P0 | Directed weighted graph | Mandatory |
| P0 | CSR graph representation | Mandatory |
| P0 | Custom binary heap | Mandatory |
| P0 | Iterative SCC extraction | Mandatory |
| P0 | Dijkstra's algorithm | Mandatory |
| P0 | A* search | Mandatory |
| P0 | Bidirectional Dijkstra | Mandatory |
| P0 | Automated correctness testing | Mandatory |
| P0 | Deterministic event simulator | Mandatory |
| P0 | Basic dispatch policies | Mandatory |
| P0 | Event logging and replay | Mandatory |
| P0 | Local web visualization | Mandatory |
| P0 | Performance benchmarks | Mandatory |
| P1 | CSR versus adjacency-list comparison | Required for evaluation |
| P1 | Multiple synthetic scenarios | Required for evaluation |
| P1 | Reproducible experiment runner | Required for evaluation |
| P2 | Customizable Contraction Hierarchies | Stretch goal |
| P2 | Colorado road-network experiments | Conditional stretch |
| P2 | Research-oriented routing optimization | Post-submission exploration |

**Explicit exclusions:** ALT routing, min-cost-flow dispatch, order batching, H3 indexing, advanced CCH customization variants, and production deployment.

These features are outside the agreed submission scope.

---

# 4. Functional Requirements

## FR-01: Road Network Import

### Description

DispatchLab must import real road-network data from OpenStreetMap.

### Geographic coverage

The initial dataset covers:

- Chandigarh
- Mohali
- Panchkula

The resulting graph represents roads that can reasonably be used by delivery vehicles.

### Required functionality

The importer must:

1. Read an OSM `.pbf` file using `libosmium`.
2. Extract drivable roads.
3. Respect one-way restrictions.
4. Calculate edge lengths from geographic coordinates.
5. Assign travel-time weights.
6. Remap large OSM node identifiers to compact integer identifiers.
7. Identify and retain the largest strongly connected component.
8. Convert the resulting graph into CSR representation.
9. Serialize the graph into a binary file.

### Edge weights

Each directed edge represents travel time.

\[
w(u,v)=\frac{d(u,v)}{s(u,v)}
\]

Where:

- \(d(u,v)\) is edge length.
- \(s(u,v)\) is the assumed vehicle speed.
- \(w(u,v)\) is travel time.

When reliable speed information is unavailable, documented default speeds are assigned based on road classification.

Travel times should be stored as positive integer values, such as milliseconds, to support reproducibility.

### Acceptance criteria

The importer must successfully generate a graph in which:

- Every edge references valid nodes.
- Edge weights are positive.
- One-way restrictions are respected.
- The retained component is strongly connected.
- OSM identifiers are mapped consistently.
- Serialization and deserialization preserve the graph.

**Deliverable:** `tricity.graph`

---

## FR-02: Graph Representation

### Description

The graph engine must use Compressed Sparse Row (CSR) representation.

### Required structure

```cpp
struct Graph {
    vector<uint32_t> offsets;
    vector<uint32_t> targets;
    vector<uint32_t> weights;
};
```

Additional structures may be used for geographic coordinates, reverse edges, and metadata.

For a graph with \(V\) vertices and \(E\) edges, CSR provides:

\[
O(V+E)
\]

storage complexity.

### Requirements

Implement:

- CSR construction
- Outgoing-edge traversal
- Reverse graph construction
- Node identifier mapping
- Binary serialization
- Graph integrity checks

An adjacency-list baseline should also be implemented for comparison.

### Performance evaluation

Compare:

| Metric | CSR | Adjacency list |
|---|---|---|
| Memory usage | Measure | Measure |
| Graph loading time | Measure | Measure |
| Sequential edge traversal | Measure | Measure |
| Dijkstra query time | Measure | Measure |

No particular speedup is assumed in advance.

---

## FR-03: Custom Binary Heap

### Description

Implement a binary min-heap rather than relying exclusively on `std::priority_queue`.

### Operations

```cpp
push()
pop()
top()
empty()
```

Optional optimization:

```cpp
decrease_key()
```

A lazy duplicate-entry approach is acceptable for Dijkstra and A* if stale entries are correctly discarded.

### Complexity

| Operation | Complexity |
|---|---|
| Insert | O(log n) |
| Extract minimum | O(log n) |
| Peek | O(1) |
| Empty check | O(1) |

### Acceptance criteria

The custom heap must pass randomized tests against a standard-library reference implementation.

Heap operations must preserve the heap invariant.

---

# 5. Routing Engine

The routing engine is the algorithmic foundation of DispatchLab.

## FR-04: Dijkstra's Algorithm

### Description

Implement exact shortest-path routing on a directed graph with nonnegative edge weights.

### Requirements

The implementation must support:

- Source and destination identifiers
- Shortest travel-time calculation
- Path reconstruction
- Unreachable destinations
- Custom binary heap
- Integer weight accumulation with overflow protection

### Expected complexity

With a binary heap:

\[
O((V+E)\log V)
\]

This is the conventional upper bound; tighter expressions are possible.

### Acceptance criteria

Dijkstra must return the correct shortest-path distance for all deterministic test fixtures.

It becomes the reference implementation against which other routing algorithms are compared.

---

## FR-05: Bidirectional Dijkstra

### Description

Implement Dijkstra searches from both the source and destination.

The backward search must use the reversed directed graph.

### Requirements

- Forward search
- Reverse search
- Correct termination condition
- Meeting-distance calculation
- Route reconstruction

### Evaluation

Compare against regular Dijkstra using:

- Nodes settled
- Query latency
- Memory overhead
- Path cost

### Acceptance criteria

Bidirectional Dijkstra must return the same shortest-path costs as ordinary Dijkstra.

---

## FR-06: A* Search

### Description

Implement A* using a geographic lower-bound heuristic.

\[
f(n)=g(n)+h(n)
\]

Where:

- \(g(n)\): shortest known cost from the source
- \(h(n)\): lower bound on remaining travel time

A suitable heuristic is:

\[
h(n)=\frac{d_{\text{geodesic}}(n,t)}{v_{\max}}
\]

Here, \(v_{\max}\) must be an upper bound on the speeds permitted by the graph-weight model.

The heuristic must not overestimate the remaining travel time.

### Evaluation

Compare:

- Dijkstra
- Bidirectional Dijkstra
- A*

Using the same graph and source-destination pairs.

### Required measurements

| Metric | Description |
|---|---|
| Median latency | Typical routing query |
| p95 latency | Slow routing queries |
| p99 latency | Tail latency |
| Nodes settled | Search effort |
| Memory usage | Algorithm overhead |
| Correctness | Agreement with Dijkstra |

### Acceptance criteria

A* must return exactly the same shortest-path cost as Dijkstra for every validation query.

A* is not automatically considered better. Any performance claim must be supported by benchmarks.

---

# 6. Deterministic Delivery Simulator

## FR-07: Discrete-Event Simulation

### Description

Implement an event-driven simulation engine capable of modeling delivery orders and courier movement.

The simulator must not depend on wall-clock timing.

Instead, simulated time advances from one event to the next.

### Entities

**Customer**

```cpp
struct Customer {
    int id;
    int location;
};
```

**Restaurant**

```cpp
struct Restaurant {
    int id;
    int location;
};
```

**Courier**

```cpp
struct Courier {
    int id;
    int location;
    bool available;
};
```

**Order**

```cpp
struct Order {
    int id;
    int restaurant_id;
    int customer_id;
    int created_at;
    int preparation_time;
};
```

These are conceptual schemas. Final implementations should include the state needed for active deliveries, timing, and deterministic identifiers.

### Events

The simulator must support:

1. Order creation
2. Food preparation completion
3. Courier assignment
4. Courier arrival at restaurant
5. Order pickup
6. Order delivery
7. Courier becoming available

### Event handling

Events are stored in a priority queue ordered by:

\[
(\text{timestamp},\text{event priority},\text{event ID})
\]

The additional ordering fields guarantee deterministic handling of simultaneous events.

### Example

```text
00:00 Order #102 created
00:00 Restaurant begins preparation
00:01 Courier #8 assigned
00:06 Courier #8 arrives at restaurant
00:09 Food preparation completed
00:09 Courier #8 picks up order
00:23 Order #102 delivered
00:23 Courier #8 becomes available
```

### Requirements

The simulator must:

- Use simulated time rather than real time.
- Process events in a deterministic order.
- Track courier availability.
- Model restaurant preparation time.
- Calculate travel time using the routing engine.
- Queue orders when no courier is available.
- Generate an event log.
- Produce a final state hash.

### Acceptance criteria

Running the same scenario with the same seed and configuration must produce identical event sequences and final state hashes.

No order may be delivered twice.

No courier may simultaneously execute two deliveries in the initial single-order courier model.

---

# 7. Dispatch Optimization

## FR-08: Nearest-Courier Baseline

### Description

Implement a simple greedy baseline.

For every incoming order, select the available courier geographically closest to the restaurant.

\[
c^*=\arg\min_{c\in C_{\text{available}}}
d_{\text{geo}}(c,r)
\]

Where:

- \(c\): courier
- \(r\): restaurant
- \(d_{\text{geo}}\): geographic distance

The actual delivery still uses the road-routing engine.

This distinction makes the baseline computationally simple while allowing evaluation against road-network-aware dispatch.

---

## FR-09: Greedy ETA Dispatch

### Description

Implement a policy that selects couriers using estimated delivery completion time.

The policy must consider:

- Travel time to the restaurant
- Restaurant preparation completion
- Travel time from restaurant to customer
- Courier availability

For an available courier \(c\):

\[
ETA(c,o)=
\max(t_{\text{now}}+T(c,r),t_{\text{ready}})
+T(r,d)
\]

Where:

- \(T(c,r)\): road travel time from courier to restaurant
- \(T(r,d)\): road travel time from restaurant to customer
- \(t_{\text{ready}}\): food-ready timestamp

Choose:

\[
c^*=\arg\min_c ETA(c,o)
\]

For this version, selection is restricted to available couriers.

### Important limitation

This is a greedy heuristic, not a globally optimal assignment algorithm.

It may improve average delivery time but can produce worse overall performance under some demand patterns.

The benchmark must establish actual outcomes.

---

## FR-10: Policy Comparison

### Description

The experiment runner must compare dispatch strategies under equivalent initial conditions.

### Mandatory policies

| Policy | Strategy |
|---|---|
| Nearest | Minimum geographic pickup distance |
| Greedy ETA | Minimum estimated delivery completion time |

### Controlled inputs

Both policies must receive identical:

- Road networks
- Orders
- Customer locations
- Restaurant locations
- Preparation times
- Initial courier locations
- Traffic scenarios
- Random seeds

The generator must create the scenario once and reuse it rather than allowing individual policies to generate different workloads.

### Evaluation metrics

| Metric | Description |
|---|---|
| Average delivery time | Mean order creation-to-delivery duration |
| p95 delivery time | Tail delivery duration |
| Courier utilization | Fraction of operating time couriers are busy |
| Total travel distance | Aggregate courier movement |
| Assignment latency | Computation time for dispatch decisions |
| Completed orders | Successfully delivered orders |
| Undelivered orders | Orders remaining at simulation end |

Results must be presented without assuming that the more sophisticated policy always wins.

---

# 8. Traffic Model

## FR-11: Synthetic Traffic Scenarios

Traffic is modeled using edge-weight multipliers.

\[
w'(e)=w(e)\times m(e)
\]

With:

\[
m(e)\geq1
\]

### Required scenarios

| Scenario | Description |
|---|---|
| Free flow | All multipliers equal 1 |
| Moderate congestion | Selected roads receive moderate multipliers |
| Heavy congestion | Selected roads receive larger multipliers |

The specific multiplier distributions must be documented.

Traffic is synthetic, not a representation of measured real-time Chandigarh traffic.

For the initial version, traffic weights remain fixed within each simulation run. This avoids introducing continuous traffic updates before the core algorithms are validated.

---

# 9. Logging and Replay

## FR-12: Event Logging

Every meaningful simulation state transition must be recorded.

### Example JSON

```json
{
  "timestamp": 540,
  "event": "ORDER_PICKED_UP",
  "order_id": 102,
  "courier_id": 8,
  "location": 1523
}
```

### Requirements

Logs must include enough information to reconstruct the simulation.

A replay mechanism must validate deterministic state transitions.

Use a specified stable hashing algorithm over a canonical state representation, rather than relying on implementation-dependent hashing behavior.

### Acceptance criteria

A simulation rerun with the same configuration, seed, and build must produce the same canonical final state hash.

The event log must also support reconstructing the state without running dispatch decisions again.

---

# 10. Web Visualization

## FR-13: Local Simulation Viewer

### Description

Create a lightweight local webpage for viewing simulation results.

The webpage is a visualization layer, not the primary application.

The routing engine and simulator remain implemented in C++.

### Required components

**1. Map visualization**

Display the road network and relevant delivery locations.

**2. Courier visualization**

Show courier positions and statuses.

**3. Order visualization**

Show restaurants, customers, and active deliveries.

**4. Event timeline**

Display simulation events chronologically.

**5. Playback controls**

Provide:

- Play
- Pause
- Replay
- Playback speed
- Timeline scrubbing

**6. Metrics panel**

Display total orders, completed deliveries, average delivery time, and available couriers.

### Architecture

```text
C++ Simulator
     |
     v
Replay JSON
     |
     v
HTML + CSS + JavaScript
     |
     v
Interactive Visualization
```

The viewer must operate locally without requiring a deployed backend.

A simple offline SVG or Canvas road-network visualization is sufficient.

A commercial map API is not required.

### Acceptance criteria

A user must be able to run the simulator, open the generated replay locally, and inspect courier movements and order states.

---

# 11. Advanced Routing: CCH

## FR-14: Customizable Contraction Hierarchies

**Priority: P2 — Stretch goal**

### Objective

Investigate whether preprocessing the road network can substantially reduce shortest-path query latency.

Ordinary Dijkstra explores the graph during every query.

Contraction-based methods perform additional preprocessing so subsequent queries can explore a smaller search space.

### Proposed implementation

The CCH implementation would include:

1. Node ordering using an external nested-dissection ordering tool such as `ndmetis`.
2. Contraction topology construction.
3. Triangle enumeration or another correct customization procedure.
4. Basic metric customization.
5. Upward shortest-path queries.
6. Path unpacking.

Only basic customization is within scope.

Perfect customization, parallel customization, and partial customization are excluded.

### Evaluation

Compare:

| Metric | Dijkstra | A* | CCH |
|---|---|---|---|
| Preprocessing time | Measure | Measure | Measure |
| Query latency | Measure | Measure | Measure |
| Memory footprint | Measure | Measure | Measure |
| Path correctness | Exact | Exact | Exact |

The experiment must separate one-time preprocessing costs from query costs.

### CCH kill-switch

**Decision deadline: November 2, 2026.**

CCH development must stop if either of the following is true:

- The implementation cannot produce exact results on small deterministic fixtures.
- The implementation cannot correctly answer at least 100 Tricity test queries.

If CCH fails, it is removed from the submission's critical path.

The replacement focus becomes a rigorous graph-layout and priority-queue performance study using the completed routing algorithms.

This prevents the advanced algorithm from jeopardizing the entire project.

### Additional dataset

A Colorado road-network dataset may be introduced for generalization experiments if the CCH implementation succeeds.

It is not required for the ADSA submission.

---

# 12. System Architecture

## 12.1 High-level architecture

```text
                  OpenStreetMap PBF
                         |
                         v
                 +----------------+
                 |   OSM Importer |
                 |    libosmium   |
                 +----------------+
                         |
                         v
                 +----------------+
                 |  Graph Builder |
                 |                |
                 | Directed graph |
                 | SCC extraction |
                 | Node remapping |
                 | CSR conversion |
                 +----------------+
                         |
                         v
                 +----------------+
                 |  Graph Storage |
                 |  Binary Format |
                 +----------------+
                         |
                         v
              +----------------------+
              |     Routing Engine   |
              |                      |
              | Dijkstra             |
              | Bidirectional        |
              | A*                   |
              | Optional CCH         |
              +----------------------+
                         |
                         v
              +----------------------+
              |   Dispatch Engine    |
              |                      |
              | Nearest              |
              | Greedy ETA           |
              +----------------------+
                         |
                         v
              +----------------------+
              |   Event Simulator    |
              |                      |
              | Event queue          |
              | Courier states       |
              | Order states         |
              | Deterministic clock  |
              +----------------------+
                         |
              +----------+----------+
              |                     |
              v                     v
       +-------------+       +-------------+
       | Experiment  |       | Event Logs  |
       | Runner      |       | Replay JSON |
       +-------------+       +-------------+
              |                     |
              v                     v
       +-------------+       +-------------+
       | Benchmarks  |       | Web Viewer  |
       | Reports     |       | HTML / JS   |
       +-------------+       +-------------+
```

## 12.2 Proposed repository structure

```text
dispatchlab/
│
├── CMakeLists.txt
├── README.md
├── PERFORMANCE.md
│
├── include/
│   └── dispatchlab/
│       ├── graph.hpp
│       ├── binary_heap.hpp
│       ├── routing.hpp
│       ├── simulator.hpp
│       └── dispatch.hpp
│
├── src/
│   ├── graph/
│   │   ├── osm_importer.cpp
│   │   ├── csr_graph.cpp
│   │   └── scc.cpp
│   │
│   ├── routing/
│   │   ├── dijkstra.cpp
│   │   ├── bidirectional.cpp
│   │   ├── astar.cpp
│   │   └── cch.cpp
│   │
│   ├── simulation/
│   │   ├── event_queue.cpp
│   │   ├── simulator.cpp
│   │   └── replay.cpp
│   │
│   ├── dispatch/
│   │   ├── nearest.cpp
│   │   └── greedy_eta.cpp
│   │
│   └── main.cpp
│
├── tests/
│   ├── test_graph.cpp
│   ├── test_heap.cpp
│   ├── test_routing.cpp
│   ├── test_simulator.cpp
│   └── test_replay.cpp
│
├── benchmarks/
│   ├── benchmark_graph.cpp
│   ├── benchmark_routing.cpp
│   └── benchmark_dispatch.cpp
│
├── experiments/
│   ├── configs/
│   ├── scripts/
│   └── results/
│
├── viewer/
│   ├── index.html
│   ├── style.css
│   └── app.js
│
└── data/
    ├── raw/
    ├── processed/
    └── scenarios/
```

The CCH source file should only become part of the active build if that optional workstream proceeds.

---

# 13. Testing and Quality Assurance

Correctness takes precedence over performance.

## 13.1 Routing correctness

Generate at least **10,000 source-destination pairs** using a fixed random seed.

For every pair, compare:

\[
D_{\text{Dijkstra}}(s,t)
=
D_{\text{Bidirectional}}(s,t)
=
D_{\text{A*}}(s,t)
\]

All algorithms must return equal integer path costs.

Reconstructed paths must also be checked by summing their constituent edge weights.

If CCH is implemented:

\[
D_{\text{CCH}}(s,t)=D_{\text{Dijkstra}}(s,t)
\]

It must pass the same correctness suite.

## 13.2 Unit tests

| Component | Tests |
|---|---|
| Binary heap | Insert, extract, duplicates, empty |
| Graph | Directed edges, weights, serialization |
| SCC | Known components and connectivity |
| Dijkstra | Exact distances and unreachable nodes |
| Bidirectional | Equality with Dijkstra |
| A* | Equality with Dijkstra |
| Event queue | Stable timestamp ordering |
| Simulator | Valid state transitions |
| Dispatch | Valid courier assignment |
| Replay | Deterministic reconstruction |

## 13.3 Simulator invariants

The simulator must enforce:

\[
\text{DeliveredOrders}\leq\text{CreatedOrders}
\]

Each order must have at most one active courier assignment.

Delivery cannot occur before pickup.

Pickup cannot occur before the courier reaches the restaurant and the food is ready.

Simulation timestamps cannot move backward.

A courier cannot be assigned to multiple simultaneous deliveries.

These invariants must be checked automatically.

---

# 14. Performance Benchmarking

Performance engineering is a central component of DispatchLab.

## 14.1 Graph benchmarks

Measure:

- Number of vertices
- Number of directed edges
- Graph loading time
- CSR construction time
- Memory consumption
- Adjacency-list versus CSR traversal performance

## 14.2 Routing benchmarks

Evaluate:

- Dijkstra
- Bidirectional Dijkstra
- A*
- CCH, if completed

Across different source-destination distances.

Use the same query set for every algorithm.

### Required output

| Algorithm | Median | p95 | p99 | Nodes settled | Memory |
|---|---|---|---|---|---|
| Dijkstra | Measured | Measured | Measured | Measured | Measured |
| Bidirectional | Measured | Measured | Measured | Measured | Measured |
| A* | Measured | Measured | Measured | Measured | Measured |
| CCH | Optional | Optional | Optional | Optional | Optional |

Record hardware, compiler version, compiler flags, dataset version, seed, and benchmark methodology.

Use optimized release builds for performance results.

## 14.3 Dispatch benchmarks

Evaluate scenarios with varying numbers of couriers and orders.

A proposed experiment matrix is:

| Scenario | Couriers | Orders |
|---|---:|---:|
| Small | 20 | 100 |
| Medium | 100 | 1,000 |
| Large | 500 | 5,000 |

These are initial experiment sizes, not fixed performance targets. They may be adjusted based on measured runtime.

Compare dispatch policies using identical scenarios.

## 14.4 Reproducibility

Each experiment must record:

```text
Experiment ID
Dataset version
Graph build configuration
Routing algorithm
Dispatch policy
Random seed
Scenario parameters
Compiler version
Hardware information
Execution time
Results
```

The experiment runner should export results to CSV or JSON.

For policy comparisons, run multiple paired seeds where feasible, rather than drawing conclusions from a single simulation.

---

# 15. Nonfunctional Requirements

| Category | Requirement |
|---|---|
| Correctness | Exact routing and valid simulator transitions |
| Determinism | Repeated runs with identical inputs produce identical results |
| Performance | Routing and dispatch costs must be measured |
| Memory | No unexplained growth or leaks |
| Portability | Standard C++20 and CMake |
| Dependencies | Open-source libraries only |
| Network | No runtime dependence on third-party APIs |
| Visualization | Local and framework-free |
| Logging | Structured, machine-readable |
| Testing | Automated test suite |
| Documentation | Reproducible build and experiment instructions |
| Maintainability | Separation of graph, routing, simulation, and dispatch |

The reference environment can be Linux, including WSL2. macOS support is desirable but should not delay the mandatory deliverables.

---

# 16. Development Milestones

The schedule assumes approximately 12 hours of project work per week.

The milestones are targets, not claims of completed work.

## Milestone 1 — Prototype
**Deadline: October 19–23, 2026**

### Required outcome

Demonstrate the central idea end to end.

Minimum prototype:

- Import the Tricity OSM dataset.
- Construct a directed graph.
- Convert it into CSR.
- Calculate a shortest path using Dijkstra.
- Demonstrate A* on the same graph.
- Run a small synthetic delivery scenario.
- Export basic event logs.

A minimal viewer is desirable but should not block the routing demonstration.

**Definition of done:** A real road-network route can be computed and used to simulate a delivery.

---

## Milestone 2 — Complete Core Algorithms
**Target: October 24–31, 2026**

### Deliverables

- Custom binary heap
- Iterative SCC extraction
- Binary graph serialization
- Bidirectional Dijkstra
- Completed A* implementation
- 10,000-query routing correctness suite
- Simulator event ordering
- Nearest and ETA dispatch baselines

**Definition of done:** All mandatory algorithms run correctly, and dispatch policies can be compared using identical synthetic workloads.

---

## Milestone 3 — Advanced Routing Decision
**Deadline: November 2, 2026**

Determine whether CCH is technically viable.

### Continue CCH only if:

- Basic topology and query implementation are working.
- Small deterministic fixtures pass.
- At least 100 Tricity routing queries return exact results.

Otherwise, terminate CCH development.

Move to graph-layout and priority-queue performance experiments.

---

## Milestone 4 — Simulation and Evaluation
**Target: November 3–16, 2026**

### Deliverables

- Complete deterministic event simulation
- Event replay with state hashes
- Viewer with playback controls
- Experiment runner
- Dispatch comparison benchmarks
- Routing performance benchmarks
- Memory analysis
- Optional CCH experiments if the kill-switch conditions were satisfied

**Definition of done:** DispatchLab can execute reproducible experiments and generate results that explain algorithmic and operational differences.

---

## Milestone 5 — Final Submission
**Deadline: November 25, 2026**

### Deliverables

- Complete source code
- CMake build configuration
- README
- Algorithm explanations
- Complexity analysis
- Automated tests
- Benchmark results
- `PERFORMANCE.md`
- Local replay viewer
- Demo scenario
- Presentation material

**Definition of done:** Another developer can follow the README, build the project, run the tests, reproduce a simulation, and inspect its results.

---

# 17. Final Acceptance Criteria

DispatchLab is considered complete when the following gates are satisfied.

| ID | Acceptance criterion | Required |
|---|---|---|
| AC-01 | Tricity OSM data successfully imported | Yes |
| AC-02 | Directed CSR graph generated correctly | Yes |
| AC-03 | Custom binary heap passes tests | Yes |
| AC-04 | Iterative SCC implementation works | Yes |
| AC-05 | Dijkstra calculates exact shortest paths | Yes |
| AC-06 | A* agrees with Dijkstra | Yes |
| AC-07 | Bidirectional Dijkstra agrees with Dijkstra | Yes |
| AC-08 | At least 10,000 routing agreement tests pass | Yes |
| AC-09 | Simulator correctly processes delivery events | Yes |
| AC-10 | Nearest-courier policy implemented | Yes |
| AC-11 | Greedy ETA policy implemented | Yes |
| AC-12 | Identical inputs produce identical replay hashes | Yes |
| AC-13 | Local viewer displays a recorded simulation | Yes |
| AC-14 | Routing latency and memory benchmarks completed | Yes |
| AC-15 | Dispatch policies compared fairly | Yes |
| AC-16 | Project builds using documented instructions | Yes |
| AC-17 | CCH returns exact shortest-path results | No |
| AC-18 | Colorado experiments completed | No |
| AC-19 | Research paper submitted | No |

**The mandatory acceptance criteria are independent of CCH and publication outcomes.**

---

# 18. Risks and Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| Incorrect OSM parsing | High | Validate small fixtures before full ingestion |
| Invalid road connectivity | High | Iterative SCC extraction and graph checks |
| Incorrect A* heuristic | High | Prove admissibility under the weight model |
| CCH implementation complexity | High | November 2 kill-switch |
| Simulator nondeterminism | High | Integer times and explicit event tie-breaking |
| Unrealistic traffic data | Medium | Clearly identify synthetic assumptions |
| Excessive routing query costs | Medium | Benchmark and optimize only after correctness |
| Visualization consumes too much time | Medium | Static, minimal viewer |
| Dispatch policy performs poorly | Low | Report findings without requiring improvement |
| Research contribution lacks novelty | Low | No publication requirement for completion |

---

# 19. Future Research Directions

The final ADSA deliverable does not require a research contribution.

However, DispatchLab can serve as an experimental platform for one.

### Candidate: Decision-Aware Routing

A potential research direction is reducing the number of exact shortest-path computations needed during courier assignment.

Suppose there are 500 couriers and 100 incoming orders.

A naive dispatcher may evaluate thousands of courier-order travel times.

But many candidate assignments might be eliminated using inexpensive distance bounds before calculating exact routes.

The investigation would ask:

> Can a dispatch engine avoid exact route computations when lower and upper bounds are sufficient to prove that a candidate cannot improve the assignment decision?

Potential metrics include:

- Exact route queries avoided
- Dispatch computation latency
- Assignment quality
- Memory overhead
- Behavior under congestion changes

This is a **research hypothesis, not a verified novel algorithm**.

It would require a separate literature review and comparisons with existing lazy-evaluation, branch-and-bound, and routing-bound techniques before any publication claim.

It should not displace mandatory development work before November 25.

---

# 20. Project Success Definition

DispatchLab succeeds if it demonstrates three things.

### 1. Algorithmic competence

You can build an efficient graph representation, implement shortest-path algorithms, establish their correctness, and reason about complexity.

### 2. Systems engineering competence

You can design deterministic simulations, enforce invariants, create reproducible experiments, and diagnose performance bottlenecks.

### 3. Engineering judgment

You can compare alternative implementations, evaluate trade-offs, recognize when an optimization is not justified, and document evidence rather than assuming an algorithm is superior.

A technically correct system with carefully designed experiments and an unsuccessful advanced optimization is still a successful project.

## Final product statement

**DispatchLab is a deterministic, performance-oriented food-delivery simulation platform built in C++20, using real road-network data to evaluate shortest-path algorithms, dispatch policies, memory efficiency, and routing performance.**

The required final product consists of a functional road-network engine, three exact routing algorithms, a deterministic dispatch simulator, a local replay viewer, automated correctness tests, and reproducible performance experiments.

Customizable Contraction Hierarchies and research extensions remain optional.

**Scope boundary:** Ship the complete ADSA system first. Pursue advanced routing or publication only when the mandatory implementation is stable.
