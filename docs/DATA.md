# Data, models and file formats

## 1. Road network source

| Item | Value |
|---|---|
| Provider | Geofabrik GmbH, OpenStreetMap extracts |
| Parent extract | `asia/india/northern-zone-261017.osm.pbf` (covers Punjab, Haryana, Chandigarh, …) |
| Extract date | OSM data up to **2026-10-17T20:20:51Z** |
| Parent MD5 | `469effc7d5d972f69d4d32c62629e06e` (224,017,742 bytes; verified against Geofabrik's `.md5`) |
| Bounding box | **min lon 76.62, min lat 30.62, max lon 76.95, max lat 30.80** (Chandigarh, Mohali/SAS Nagar, Panchkula, Zirakpur) |
| Clip command | `osmium extract -b 76.62,30.62,76.95,30.80 --strategy complete_ways --set-bounds -o data/raw/tricity-261017.osm.pbf northern-zone-261017.osm.pbf` (osmium-tool 1.19.1 / libosmium 2.23.1) |
| Committed file | `data/raw/tricity-261017.osm.pbf` (≈3.5 MB) |
| License | © OpenStreetMap contributors, ODbL 1.0 — https://www.openstreetmap.org/copyright |

Geofabrik has no separate Punjab-only file, so the
northern-zone extract was clipped. `scripts/fetch_data.sh` reproduces the download and clip. You don't
need it to build: the clipped extract is committed, and nothing contacts the network at runtime.

`--strategy complete_ways` keeps whole ways that cross the box, so the importer also clips
(`--clip-tricity`). It keeps a road segment only when **both** endpoints lie inside the box.

## 2. Drivable-road filter

A way becomes graph edges if all of these hold:

* `highway` ∈ {motorway, motorway_link, trunk, trunk_link, primary, primary_link, secondary,
  secondary_link, tertiary, tertiary_link, unclassified, residential, living_street, service, road}
* not `area=yes`
* not `motor_vehicle=no|private`. With `access=no|private`, the way is kept only if `motor_vehicle`,
  `motorcycle` or `motorcar` is `yes|permissive|designated|destination`
* `oneway` is not `reversible`/`alternating` (time-dependent direction cannot be modelled)

Direction rules:

* `oneway=yes|true|1`: forward only
* `oneway=-1|reverse`: backward only
* `oneway=no`: both directions
* otherwise `junction=roundabout|circular` and motorway classes imply one-way; everything else is two-way

Every pair of consecutive way nodes becomes an edge. Degree-2 nodes are kept, so the viewer can draw
real geometry.

## 3. Weight model

Edge weight is travel time in integer milliseconds:

```
w(u,v) = max(1, ceil(haversine_m(u, v) * 3600 / speed_kmh(class)))
```

`haversine_m` uses the IUGG mean Earth radius of 6,371,008.8 m. Lengths are also stored, as millimetres
rounded to nearest. Default speeds approximate urban two-wheeler delivery. OSM `maxspeed` tags are
ignored on purpose: they are sparse in this extract, and a uniform documented model was preferred.

| Class | km/h | Class | km/h |
|---|---:|---|---:|
| motorway | 80 | tertiary | 35 |
| motorway_link | 50 | tertiary_link | 25 |
| trunk | 60 | unclassified | 25 |
| trunk_link | 40 | residential | 20 |
| primary | 50 | living_street | 10 |
| primary_link | 35 | service | 15 |
| secondary | 40 | road | 20 |
| secondary_link | 30 | | |

Parallel edges between the same pair of nodes are merged, keeping the minimum weight. Self-loops are
dropped. Only the **largest strongly connected component** is kept (iterative Tarjan).
`GraphMeta::max_speed_kmh` is set to the fastest class that survives SCC extraction: 60 km/h (trunk) for
the Tricity graph.

### A* admissibility

A* uses `h(n) = floor(haversine_m(n, t) * 3600 / v_max)` with `v_max = max_speed_kmh`. For every edge,
`w(e) ≥ len(e)·3600/s(e) ≥ len(e)·3600/v_max`, because weights round up and `s(e) ≤ v_max`. The
geodesic distance to `t` is at most the length of any path to `t` (triangle inequality on the sphere).
So `h` never overestimates. Because `h` rounds down and weights are integers, `h` is also consistent,
up to floating-point rounding. As a safety net, A* re-opens nodes when it finds a cheaper `g`. Traffic
multipliers are ≥ 1, so `h` stays admissible under every traffic scenario. `validate()` checks
`len·3.6 ≤ v_max·w` (with 0.5 mm slack for length rounding) on every edge. A unit test asserts
`h(v) ≤ d(v, t)` on sampled nodes.

## 4. Traffic scenarios (synthetic)

`w'(e) = ceil(w(e) · m(e) / 1000)`, with `m` in per-mille and `m ≥ 1000`. Weights are fixed for a whole
run. Each directed edge draws independently from `Rng(splitmix(seed ⊕ edgeid·φ))`. Arterial means
trunk/primary/secondary and their links.

| Scenario | Arterial edges | Other edges |
|---|---|---|
| `free` | m = 1.00 | m = 1.00 |
| `moderate` | with p = 0.50, m ~ U[1.20, 1.60] | with p = 0.20, m ~ U[1.05, 1.20] |
| `heavy` | with p = 0.80, m ~ U[1.60, 2.50] | with p = 0.40, m ~ U[1.10, 1.50] |

These multipliers are illustrative assumptions, not measurements of Chandigarh traffic.

## 5. Scenario (workload) model

`gen-scenario` / `generate_scenario()` uses one seeded xoshiro256** generator (no `std::` distributions):

* restaurants: `clamp(orders/10, 5, 200)` uniform random graph nodes
* couriers: uniform random start nodes
* order times: `orders` i.i.d. uniform integers on `[0, window)`, sorted. That is a Poisson process
  conditioned on its count, using integer arithmetic only
* restaurant: uniform; preparation time: uniform integer ms in `[prep_min, prep_max]` (8–20 min)
* customer: uniform node within `max_delivery_km` (6 km) geodesic of the restaurant (rejection sampling)
* simulation ends at `window + drain` (drain = 120 min); orders not delivered by then count as undelivered

The scenario JSON stores an FNV-1a hash of its canonical content. Loading verifies it. Every policy
in an experiment cell runs on the **same in-memory scenario object**.

## 6. Simulation semantics

* Time is integer milliseconds. The queue orders events by `(timestamp, priority, event id)`, with
  priority `ORDER_DELIVERED < COURIER_AVAILABLE < FOOD_READY < COURIER_ARRIVED_RESTAURANT <
  ORDER_PICKED_UP < ORDER_CREATED < COURIER_ASSIGNED`. A courier freed at time t can therefore take an
  order created at time t.
* Pending orders sit in a FIFO queue. Whenever an order is created or a courier becomes available, the
  oldest pending order is offered to the policy along with all idle couriers, ascending by id. This
  repeats until one of the two lists is empty.
* A courier carries one order at a time. Travel times come from the routing engine on the
  traffic-adjusted graph (A* by default; all three algorithms return identical costs). Travel distance is
  charged when a leg starts.
* Every transition goes through `StateMachine::apply`, which enforces PRD §13.3 and fails on any
  violation. Replay uses the same state machine, fed only by the log.

## 7. File formats

* **`*.graph` (DLGRAPH1)**: little-endian. Magic `DLGRAPH1`, u32 version, u32 n, u32 m,
  u32 max_speed_kmh, u64 + bytes source label. Then offsets[n+1], targets[m], weights[m], lengths_mm[m]
  (u32), road_class[m] (u8), lat[n], lon[n] (f64), osm_ids[n] (u64). The file ends with an FNV-1a 64
  checksum of all preceding bytes. The reverse CSR is rebuilt on load, and the graph is validated.
* **Event log (`*.jsonl`)**: one JSON object per line with fixed key order: `seq, timestamp, event,
  order_id, courier_id, location`. Travel events add `from, to, travel_ms, distance_mm[, path]`.
* **State hash**: FNV-1a 64 over `"DLSTATE1"`, the clock, counters, every courier (location, status,
  order, busy_since, busy_ms, leg_end, distance_mm, deliveries) and every order (status, courier,
  flags, six timestamps). The serialization is canonical, fixed-width and little-endian.
* **Viewer replay (`dispatchlab-replay-1`)**: `meta`, `bbox`, flat `nodes` [lon, lat, …], undirected
  `edges` [u, v, …], `edge_arterial`, entities, orders and `events` (log objects with paths).
  `replay_data.js` wraps the same document as `window.DISPATCHLAB_REPLAY = …;`, so `index.html` works
  from `file://`.
