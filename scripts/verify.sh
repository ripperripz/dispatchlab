#!/usr/bin/env bash
# DispatchLab acceptance verification (PRD §17, AC-01 .. AC-16).
#
# Performs a clean Release build and checks every mandatory acceptance
# criterion. Exits 0 only if every check passes. Usage:
#
#   ./scripts/verify.sh            # full verification
#   VERIFY_JOBS=4 ./scripts/verify.sh
#
# Nothing here is allowed to be weakened to make it pass (see PROGRESS.md).

set -u -o pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD="$ROOT/build-verify"
OUT="$BUILD/verify-out"
JOBS="${VERIFY_JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) | head -1)}"
PBF="data/raw/tricity-261017.osm.pbf"
GRAPH="data/processed/tricity.graph"
ROUTING_QUERIES=10000
ROUTING_SEED=20261018
SIM_SEED=42
EXPERIMENT_SEEDS="1 2 3 4 5"

declare -a NAMES=()
declare -a STATUS=()
FAILED=0

log() { printf '\n=== %s\n' "$*"; }

record() {  # record <id/name> <0|nonzero>
    NAMES+=("$1")
    if [ "$2" -eq 0 ]; then
        STATUS+=("PASS")
    else
        STATUS+=("FAIL")
        FAILED=1
    fi
    printf -- '--> %s: %s\n' "$1" "${STATUS[${#STATUS[@]}-1]}"
}

# run <logfile> <cmd...>: run a command, tee output to a log, return its status
run() {
    local logfile="$1"
    shift
    echo "+ $*"
    "$@" >"$logfile" 2>&1
    local rc=$?
    tail -n 25 "$logfile"
    return $rc
}

DL="$BUILD/dispatchlab"
TESTS="$BUILD/dispatchlab_tests"

# ----------------------------------------------------------------------------
log "BUILD: clean Release configure + build (C++20, -Wall -Wextra -Werror)"
rm -rf "$BUILD"
mkdir -p "$BUILD"
mkdir -p "$OUT"
BUILD_OK=0
run "$OUT/configure.log" cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DDISPATCHLAB_WERROR=ON || BUILD_OK=1
if [ $BUILD_OK -eq 0 ]; then
    run "$OUT/build.log" cmake --build "$BUILD" -j "$JOBS" || BUILD_OK=1
fi
# Any compiler warning in project code fails the build check (-Werror makes
# them errors already; this also catches warnings from non-Werror targets).
if grep -E "^($ROOT/)?(src|include|tests|benchmarks)/.*warning:" "$OUT/build.log" >/dev/null 2>&1; then
    echo "warnings found in project code:"
    grep -E "warning:" "$OUT/build.log" | head -20
    BUILD_OK=1
fi
grep -q -- "-std=c++20\|-std=gnu++20" "$BUILD/compile_commands.json" 2>/dev/null || { echo "not compiled as C++20"; BUILD_OK=1; }
grep -q -- "-Wall" "$BUILD/compile_commands.json" 2>/dev/null && grep -q -- "-Wextra" "$BUILD/compile_commands.json" 2>/dev/null \
    || { echo "-Wall -Wextra missing"; BUILD_OK=1; }
grep -q 'CMAKE_BUILD_TYPE:STRING=Release' "$BUILD/CMakeCache.txt" || { echo "not a Release build"; BUILD_OK=1; }
record "BUILD (Release, C++20, -Wall -Wextra, no warnings)" $BUILD_OK

if [ $BUILD_OK -ne 0 ]; then
    echo "Build failed; remaining checks cannot run."
    for id in AC-01 AC-02 AC-03 AC-04 AC-05 AC-06 AC-07 AC-08 AC-09 AC-10 AC-11 AC-12 AC-13 AC-14 AC-15 AC-16 AC-17; do
        record "$id" 1
    done
    exit 1
fi

# ----------------------------------------------------------------------------
log "TESTS: full ctest suite (unit tests, Tricity import, 10k routing agreement)"
rc=0
(cd "$BUILD" && ctest --output-on-failure -j "$JOBS") >"$OUT/ctest.log" 2>&1 || rc=1
tail -n 15 "$OUT/ctest.log"
grep -q "100% tests passed, 0 tests failed" "$OUT/ctest.log" || rc=1
# No test may be disabled, skipped or marked will-fail.
if grep -E "Not Run|Disabled|Skipped|\*\*\*Skipped" "$OUT/ctest.log" >/dev/null; then rc=1; fi
if grep -R -n -E "WILL_FAIL|DISABLED|doctest::skip|doctest::may_fail|doctest::should_fail|doctest::expected_failures" \
    CMakeLists.txt tests >/dev/null; then
    echo "expected-fail / skip markers found in tests"
    rc=1
fi
"$TESTS" --count >"$OUT/count.log" 2>&1 || true
record "TESTS (ctest: zero failures, nothing skipped)" $rc

# Unit-test groups that back individual acceptance criteria.
unit() {  # unit <logname> <doctest test-case filter>
    "$TESTS" --test-case="$2" >"$OUT/$1.log" 2>&1 && grep -q "Status: SUCCESS" "$OUT/$1.log" &&
        ! grep -q "test cases: *0 |" "$OUT/$1.log"
}

# ----------------------------------------------------------------------------
log "AC-01: Tricity OSM data imported (libosmium, committed clipped extract)"
rc=0
mkdir -p data/processed
run "$OUT/import.log" "$DL" import "$PBF" "$GRAPH" --clip-tricity \
    --source "Geofabrik asia/india/northern-zone 2026-10-17T20:20:51Z" || rc=1
nodes=$(sed -n 's/.*largest_scc nodes=\([0-9]*\).*/\1/p' "$OUT/import.log")
[ -n "$nodes" ] && [ "$nodes" -gt 100000 ] || { echo "unexpectedly small graph: '$nodes' nodes"; rc=1; }
unit ac01_osm "osm import:*" || rc=1
record "AC-01 Tricity OSM data successfully imported" $rc

log "AC-02: directed CSR graph generated correctly"
rc=0
run "$OUT/info.log" "$DL" info "$GRAPH" || rc=1
grep -q "validate=OK strongly_connected=yes scc_components=1 osm_ids_sorted_unique=yes" "$OUT/info.log" || rc=1
unit ac02_graph "graph:*" || rc=1
record "AC-02 Directed CSR graph generated correctly" $rc

log "AC-03: custom binary heap (randomized vs std::priority_queue)"
rc=0; unit ac03_heap "heap:*" || rc=1; tail -n 3 "$OUT/ac03_heap.log"
record "AC-03 Custom binary heap passes tests" $rc

log "AC-04: iterative SCC"
rc=0; unit ac04_scc "scc:*" || rc=1; tail -n 3 "$OUT/ac04_scc.log"
grep -q "scc_components=1" "$OUT/info.log" || rc=1
record "AC-04 Iterative SCC implementation works" $rc

log "AC-05: Dijkstra exact on deterministic fixtures"
rc=0; unit ac05_dijkstra "dijkstra:*" || rc=1; tail -n 3 "$OUT/ac05_dijkstra.log"
unit ac05_oracle "routing: all algorithms match Floyd-Warshall*" || rc=1
record "AC-05 Dijkstra calculates exact shortest paths" $rc

# ----------------------------------------------------------------------------
log "AC-08 (+AC-06, AC-07): ${ROUTING_QUERIES} seeded Tricity queries, Dijkstra == Bidirectional == A*, path sums == cost"
rc=0
run "$OUT/agreement.log" "$BUILD/routing_agreement" "$GRAPH" --queries "$ROUTING_QUERIES" --seed "$ROUTING_SEED" || rc=1
grep -q "ROUTING_AGREEMENT_PASS ${ROUTING_QUERIES}$" "$OUT/agreement.log" || rc=1
checked=$(sed -n 's/.* checked=\([0-9]*\).*/\1/p' "$OUT/agreement.log")
[ -n "$checked" ] && [ "$checked" -ge 10000 ] || rc=1
AGREE=$rc
record "AC-08 At least 10,000 routing agreement tests pass" $AGREE

rc=$AGREE; unit ac06_astar "astar:*" || rc=1
record "AC-06 A* agrees with Dijkstra (incl. admissibility test)" $rc
rc=$AGREE; unit ac07_bidir "bidirectional:*" || rc=1
record "AC-07 Bidirectional Dijkstra agrees with Dijkstra" $rc

log "AC-17 (optional, P2): CCH exact on fixtures and on ${ROUTING_QUERIES} Tricity queries"
rc=0
unit ac17_cch "cch:*" || rc=1
run "$OUT/agreement_cch.log" "$BUILD/routing_agreement" "$GRAPH" --queries "$ROUTING_QUERIES" --seed "$ROUTING_SEED" --cch || rc=1
grep -q "ROUTING_AGREEMENT_PASS ${ROUTING_QUERIES}$" "$OUT/agreement_cch.log" && grep -q "cch=yes" "$OUT/agreement_cch.log" || rc=1
record "AC-17 (optional) CCH returns exact shortest-path results" $rc

# ----------------------------------------------------------------------------
log "AC-09 / AC-12: deterministic simulation, replay and invariants"
SCEN="$OUT/scenario_det.json"
rc=0
run "$OUT/gen.log" "$DL" gen-scenario "$GRAPH" "$SCEN" --couriers 40 --orders 300 --seed "$SIM_SEED" --traffic moderate || rc=1
run "$OUT/sim1.log" "$DL" simulate "$GRAPH" "$SCEN" --policy greedy_eta --log "$OUT/run1.jsonl" --summary "$OUT/run1.json" || rc=1
run "$OUT/sim2.log" "$DL" simulate "$GRAPH" "$SCEN" --policy greedy_eta --log "$OUT/run2.jsonl" --summary "$OUT/run2.json" || rc=1
h1=$(sed -n 's/.*final_state_hash=\([0-9a-f]*\).*/\1/p' "$OUT/sim1.log")
h2=$(sed -n 's/.*final_state_hash=\([0-9a-f]*\).*/\1/p' "$OUT/sim2.log")
echo "run1 hash=$h1 run2 hash=$h2"
[ -n "$h1" ] && [ "$h1" = "$h2" ] || { echo "final state hashes differ"; rc=1; }
cmp -s "$OUT/run1.jsonl" "$OUT/run2.jsonl" || { echo "event logs differ"; rc=1; }
[ -s "$OUT/run1.jsonl" ] || rc=1
events=$(wc -l <"$OUT/run1.jsonl" | tr -d ' ')
echo "events logged: $events"
[ "${events:-0}" -gt 300 ] || rc=1
# All seven event types must occur.
for ev in ORDER_CREATED FOOD_READY COURIER_ASSIGNED COURIER_ARRIVED_RESTAURANT ORDER_PICKED_UP ORDER_DELIVERED COURIER_AVAILABLE; do
    grep -q "\"event\":\"$ev\"" "$OUT/run1.jsonl" || { echo "missing event type $ev"; rc=1; }
done
# Replay from the log alone (no routing, no dispatch) must reproduce the hash.
run "$OUT/replay.log" "$DL" replay "$SCEN" "$OUT/run1.jsonl" --expect "$h1" || rc=1
hr=$(sed -n 's/.*replayed_state_hash=\([0-9a-f]*\).*/\1/p' "$OUT/replay.log")
[ -n "$hr" ] && [ "$hr" = "$h1" ] || { echo "replay hash mismatch: '$hr' vs '$h1'"; rc=1; }
grep -q "dispatch_calls=0 routing_calls=0" "$OUT/replay.log" || { echo "replay invoked dispatch/routing"; rc=1; }
# A different seed must give a different hash (the hash is not a constant).
run "$OUT/gen_b.log" "$DL" gen-scenario "$GRAPH" "$OUT/scenario_b.json" --couriers 40 --orders 300 --seed $((SIM_SEED + 1)) --traffic moderate || rc=1
run "$OUT/sim_b.log" "$DL" simulate "$GRAPH" "$OUT/scenario_b.json" --policy greedy_eta --log "$OUT/run_b.jsonl" || rc=1
hb=$(sed -n 's/.*final_state_hash=\([0-9a-f]*\).*/\1/p' "$OUT/sim_b.log")
[ -n "$hb" ] && [ "$hb" != "$h1" ] || { echo "hash insensitive to seed"; rc=1; }
DET=$rc
record "AC-12 Identical inputs produce identical event logs and replay hashes" $DET

log "AC-09: simulator processes delivery events; PRD §13.3 invariants"
rc=0
run "$OUT/inv.log" "$DL" check-log "$SCEN" "$OUT/run1.jsonl" || rc=1
grep -q "INVARIANTS_OK" "$OUT/inv.log" || rc=1
# Invariant checker must reject corrupted logs (delivery before pickup, double delivery, time travel).
run "$OUT/inv_neg.log" "$DL" check-log "$SCEN" "$OUT/run1.jsonl" --self-test-corruptions || rc=1
grep -q "CORRUPTIONS_DETECTED 5/5" "$OUT/inv_neg.log" || rc=1
grep -q "orders_delivered=" "$OUT/sim1.log" || rc=1
unit ac09_sim "simulator:*" || rc=1
unit ac09_replay "replay:*" || rc=1
unit ac09_queue "event queue:*" || rc=1
record "AC-09 Simulator correctly processes delivery events (invariants enforced)" $rc

log "AC-10 / AC-11: dispatch policies"
rc=0
run "$OUT/sim_nearest.log" "$DL" simulate "$GRAPH" "$SCEN" --policy nearest --log "$OUT/run_nearest.jsonl" || rc=1
grep -q "policy=nearest" "$OUT/sim_nearest.log" || rc=1
unit ac10_nearest "dispatch: nearest*" || rc=1
record "AC-10 Nearest-courier policy implemented" $rc
rc=0
grep -q "policy=greedy_eta" "$OUT/sim1.log" || rc=1
unit ac11_eta "dispatch: greedy eta*" || rc=1
record "AC-11 Greedy ETA policy implemented" $rc

# ----------------------------------------------------------------------------
log "AC-15: fair policy comparison (shared pre-generated scenarios, paired seeds)"
rc=0
RES="experiments/results"
mkdir -p "$RES"
run "$OUT/experiment.log" "$DL" experiment "$GRAPH" --config experiments/configs/verify.json --out "$RES" || rc=1
CSV="$RES/policy_comparison.csv"
[ -s "$CSV" ] || { echo "missing $CSV"; rc=1; }
if [ -s "$CSV" ]; then
    # Every (size, traffic, seed) row group must contain both policies with the same scenario hash.
    "$DL" check-experiment "$CSV" >"$OUT/check_experiment.log" 2>&1 || rc=1
    cat "$OUT/check_experiment.log"
    grep -q "EXPERIMENT_FAIR_OK" "$OUT/check_experiment.log" || rc=1
    for s in $EXPERIMENT_SEEDS; do
        grep -q ",$s," "$CSV" || { echo "seed $s missing"; rc=1; }
    done
fi
[ -s "$RES/policy_comparison.json" ] && [ -s "$RES/policy_summary.md" ] || rc=1
record "AC-15 Dispatch policies compared fairly" $rc

# ----------------------------------------------------------------------------
log "RESEARCH: decision-aware Greedy ETA strategies make identical decisions (docs/RESEARCH.md)"
rc=0
run "$OUT/eta.log" "$BUILD/benchmark_eta" "$GRAPH" --samples 40 --seeds 2 --couriers 20,100 \
    --sizes small,small_peak --strategies full,pruned,cch,bounded,adaptive,rphast,hybrid,auto || rc=1
grep -q "EXACTNESS_OK" "$OUT/eta.log" || rc=1
unit research_eta "dispatch: greedy eta strategies*" || rc=1
record "RESEARCH exact decision-aware dispatch (identical choices and hashes)" $rc

# ----------------------------------------------------------------------------
log "AC-14: routing + graph benchmarks -> PERFORMANCE.md"
rc=0
BENCH="benchmarks/results"
mkdir -p "$BENCH"
run "$OUT/bench_graph.log" "$BUILD/benchmark_graph" "$GRAPH" --out "$BENCH/graph.json" --seed "$ROUTING_SEED" || rc=1
run "$OUT/bench_routing.log" "$BUILD/benchmark_routing" "$GRAPH" --out "$BENCH/routing.json" --queries 2000 --seed "$ROUTING_SEED" || rc=1
run "$OUT/bench_dispatch.log" "$BUILD/benchmark_dispatch" "$GRAPH" --out "$BENCH/dispatch.json" --seed "$ROUTING_SEED" || rc=1
run "$OUT/report.log" "$DL" perf-report --graph-json "$BENCH/graph.json" --routing-json "$BENCH/routing.json" \
    --dispatch-json "$BENCH/dispatch.json" --out PERFORMANCE.md || rc=1
for pat in "Median" "p95" "p99" "Nodes settled" "Memory" "Hardware" "Compiler" "Flags" "Dataset" "Seed" \
           "CSR" "Adjacency" "Dijkstra" "Bidirectional" "A\*" "CCH"; do
    grep -q "$pat" PERFORMANCE.md || { echo "PERFORMANCE.md lacks '$pat'"; rc=1; }
done
grep -q "BEGIN GENERATED" PERFORMANCE.md || rc=1
record "AC-14 Routing latency and memory benchmarks completed" $rc

# ----------------------------------------------------------------------------
log "AC-13: replay JSON for the local viewer (no server, no external map API)"
rc=0
run "$OUT/export.log" "$DL" export-viewer "$GRAPH" "$SCEN" "$OUT/run1.jsonl" --policy greedy_eta \
    --out viewer/replay.json --js viewer/replay_data.js --results-js viewer/results_data.js || rc=1
for f in viewer/index.html viewer/style.css viewer/app.js viewer/core.js viewer/api.js viewer/map.js viewer/charts.js \
         viewer/ui.js viewer/fonts/Geist-Variable.woff2 viewer/replay.json viewer/replay_data.js viewer/results_data.js; do
    [ -s "$f" ] || { echo "missing $f"; rc=1; }
done
# No third-party network resources anywhere in the console: no external URLs
# (only the loopback console address may appear, in help text, plus the SVG
# namespace identifier, which is never fetched), no
# protocol-relative src/href, no @import, and no XHR/WebSocket/EventSource/beacon.
# fetch() may appear only in viewer/api.js, which accepts relative /api/ paths only.
VIEWER_SRC=$(find viewer -type f \( -name '*.html' -o -name '*.js' -o -name '*.mjs' -o -name '*.css' \) | sort)
if grep -n -E "https?://" $VIEWER_SRC | grep -v -E "https?://(127\.0\.0\.1|localhost)([:/\"'\` ]|$)" | grep -v -F "'http://www.w3.org/2000/svg'"; then
    echo "viewer references an external host"
    rc=1
fi
if grep -n -E "src=\"//|href=\"//|url\(//|@import|XMLHttpRequest|new WebSocket|EventSource\(|sendBeacon" $VIEWER_SRC; then
    echo "viewer uses a forbidden network mechanism"
    rc=1
fi
if grep -n "fetch(" $VIEWER_SRC | grep -v "^viewer/api.js:"; then
    echo "fetch() outside viewer/api.js"
    rc=1
fi
grep -q "if (!path.startsWith('/api/'))" viewer/api.js || { echo "api.js lost its relative-path guard"; rc=1; }
grep -q 'src="replay_data.js"' viewer/index.html || { echo "index.html does not load replay_data.js"; rc=1; }
"$DL" validate-replay viewer/replay.json >"$OUT/validate_replay.log" 2>&1 || rc=1
cat "$OUT/validate_replay.log"
grep -q "REPLAY_JSON_OK" "$OUT/validate_replay.log" || rc=1
if command -v node >/dev/null 2>&1; then
    node viewer/selftest.mjs viewer/replay.json >"$OUT/viewer_selftest.log" 2>&1 || rc=1
    cat "$OUT/viewer_selftest.log"
    grep -q "VIEWER_SELFTEST_OK" "$OUT/viewer_selftest.log" || rc=1
else
    echo "node not installed: skipping JS self-test (structure validated by validate-replay)"
fi
record "AC-13 Local viewer displays a recorded simulation" $rc

# ----------------------------------------------------------------------------
log "CONSOLE: frontend connected to the engine (dispatchlab serve, loopback only)"
rc=0
"$DL" serve "$GRAPH" --port 0 --quiet >"$OUT/serve.log" 2>&1 &
SERVE_PID=$!
PORT=""
for _ in $(seq 1 100); do
    PORT=$(sed -n 's#.*listening http://127.0.0.1:\([0-9]*\)/.*#\1#p' "$OUT/serve.log")
    [ -n "$PORT" ] && break
    sleep 0.2
done
if [ -z "$PORT" ] || ! command -v curl >/dev/null 2>&1; then
    echo "console server did not start (or curl missing)"; cat "$OUT/serve.log"; rc=1
else
    BASE="http://127.0.0.1:$PORT"
    echo "console at $BASE"
    curl -sf "$BASE/" -o "$OUT/console_index.html" && grep -q "DispatchLab Console" "$OUT/console_index.html" || { echo "index not served"; rc=1; }
    curl -sf "$BASE/views/routing.js" -o /dev/null || { echo "console scripts not served"; rc=1; }
    curl -sf "$BASE/api/info" -o "$OUT/api_info.json" || rc=1
    grep -q "\"nodes\":${nodes}" "$OUT/api_info.json" || { echo "api/info node count differs from import"; rc=1; }
    # A live route must agree across all four algorithms and match the CLI.
    curl -sf "$BASE/api/route?s=17&t=90001&trace=0" -o "$OUT/api_route.json" || rc=1
    grep -q '"agree":true' "$OUT/api_route.json" || { echo "api/route: algorithms disagree"; rc=1; }
    cli_cost=$("$DL" route "$GRAPH" 17 90001 --algo dijkstra | sed -n 's/.*cost_ms=\([0-9]*\).*/\1/p')
    grep -q "\"cost_ms\":${cli_cost}," "$OUT/api_route.json" || { echo "api/route cost differs from CLI ($cli_cost)"; rc=1; }
    # A live simulation of the verify scenario must reproduce the CLI's final state hash.
    curl -sf "$BASE/api/simulate?couriers=40&orders=300&seed=${SIM_SEED}&traffic=moderate&policy=greedy_eta" -o "$OUT/api_sim.json" || rc=1
    grep -q "\"final_state_hash\":\"${h1}\"" "$OUT/api_sim.json" || { echo "api/simulate hash differs from CLI ($h1)"; rc=1; }
    grep -q '"replay_verified":true' "$OUT/api_sim.json" || { echo "api/simulate replay not verified"; rc=1; }
    curl -sf "$BASE/api/selftest" | grep -q '"detected":5,"total":5' || { echo "api/selftest did not reject 5/5"; rc=1; }
    curl -sf "$BASE/api/results" | grep -q '"routing":{' || { echo "api/results missing benchmarks"; rc=1; }
    code=$(curl -s -o /dev/null -w "%{http_code}" --path-as-is "$BASE/../CMakeLists.txt")
    [ "$code" = "400" ] || { echo "path traversal not rejected (HTTP $code)"; rc=1; }
fi
kill "$SERVE_PID" 2>/dev/null
wait "$SERVE_PID" 2>/dev/null
record "CONSOLE live frontend connected to the engine" $rc

# ----------------------------------------------------------------------------
log "AC-16: README build/run instructions match the actual commands"
rc=0
# 1) Execute the README quickstart block verbatim (in a scratch copy of the repo).
QS="$OUT/readme_quickstart.sh"
awk '/<!-- verify:quickstart:begin -->/{f=1;next}/<!-- verify:quickstart:end -->/{f=0}f' README.md |
    sed -e '/^```/d' >"$QS"
if [ ! -s "$QS" ]; then
    echo "README quickstart block not found"
    rc=1
else
    echo "README quickstart:"
    cat "$QS"
    SCRATCH="$OUT/readme_clone"
    rm -rf "$SCRATCH"
    mkdir -p "$SCRATCH"
    # Copy tracked files only (what a fresh clone would contain).
    git ls-files -z | (cd "$ROOT" && xargs -0 tar cf -) | (cd "$SCRATCH" && tar xf -) || rc=1
    (cd "$SCRATCH" && bash -euo pipefail "$QS") >"$OUT/readme_run.log" 2>&1 || { echo "README quickstart failed"; rc=1; }
    tail -n 20 "$OUT/readme_run.log"
fi
# 2) Every `dispatchlab <cmd>` used anywhere in README must be a real subcommand.
"$DL" help >"$OUT/help.txt" 2>&1
for sub in $(grep -o -E 'dispatchlab [a-z][a-z-]+' README.md | awk '{print $2}' | sort -u); do
    grep -q -E "^  $sub( |$)" "$OUT/help.txt" || { echo "README mentions unknown subcommand: $sub"; rc=1; }
done
# 3) Every script/binary path referenced in README must exist (or be a build product).
for p in $(grep -o -E '\./(scripts|build)/[A-Za-z0-9_./-]+' README.md | sort -u); do
    case "$p" in
        ./build/*) [ -e "$BUILD/${p#./build/}" ] || { echo "README references missing build product $p"; rc=1; } ;;
        *) [ -e "$p" ] || { echo "README references missing file $p"; rc=1; } ;;
    esac
done
record "AC-16 Project builds using documented instructions" $rc

# ----------------------------------------------------------------------------
log "SUMMARY"
for i in "${!NAMES[@]}"; do
    printf '%-6s %s\n' "${STATUS[$i]}" "${NAMES[$i]}"
done
if [ $FAILED -ne 0 ]; then
    echo "VERIFY: FAILED"
    exit 1
fi
echo "VERIFY: ALL CHECKS PASSED"
exit 0
