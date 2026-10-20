// Benchmarks: the measured results written by benchmarks/*.cpp.
(function () {
  'use strict';
  const DL = window.DL;
  const { h, icon, fmt } = DL;
  DL.views = DL.views || {};

  const algoMeta = name => DL.algoFor(name);

  // Greedy ETA candidate-evaluation strategies (docs/RESEARCH.md §1 and §8).
  const STRATEGIES = [
    { id: 'full', label: 'full', note: 'Baseline: one backward Dijkstra until every courier is settled' },
    { id: 'pruned', label: 'pruned', note: 'The same search, stopped once no unsettled courier can win' },
    { id: 'bounded', label: 'bounded', note: 'Couriers in geodesic lower-bound order, exact CCH distance until the bound loses' },
    { id: 'rphast', label: 'rphast', note: 'Every courier exactly, in one restricted CCH sweep (strong baseline)' },
    { id: 'adaptive', label: 'adaptive', note: 'pruned when nodes ÷ couriers < 20,000, otherwise bounded' },
    { id: 'hybrid', label: 'hybrid', note: 'pruned under an 8,000-node budget, then bounded (negative result)' },
  ];
  const SIZE_LABEL = { small: 'Small', small_peak: 'Small peak', medium: 'Medium', medium_peak: 'Medium peak', large: 'Large', large_peak: 'Large peak' };

  // Mean decision latency per (size, strategy) and the exactness evidence: within
  // each (size, seed), every strategy must end in the same final state hash.
  function summarizeEta(run) {
    const sizes = [], cells = {}, groups = {};
    for (const r of run.system) {
      if (!sizes.includes(r.size)) sizes.push(r.size);
      const k = `${r.size}|${r.strategy}`;
      (cells[k] = cells[k] || { sum: 0, n: 0, min: Infinity, max: 0, couriers: r.couriers, orders: r.orders }).sum += r.decision_mean_us;
      cells[k].n++;
      cells[k].min = Math.min(cells[k].min, r.decision_mean_us);
      cells[k].max = Math.max(cells[k].max, r.decision_mean_us);
      (groups[`${r.size}|${r.seed}`] = groups[`${r.size}|${r.seed}`] || new Set()).add(r.final_state_hash);
    }
    const mean = (size, s) => { const c = cells[`${size}|${s}`]; return c ? c.sum / c.n : null; };
    const g = Object.values(groups);
    // Largest seed-to-seed range (max − min) as a share of the cell mean.
    const spread = Math.max(...Object.values(cells).map(c => (c.max - c.min) / (c.sum / c.n)));
    return { sizes, mean, cells, spread, runs: g.length, identical: g.filter(x => x.size === 1).length, seeds: run.system.filter(r => r.size === sizes[0] && r.strategy === run.strategies[0]).length };
  }

  DL.views.benchmarks = {
    mount(root) {
      const page = h('div.page.fade-in');
      root.appendChild(page);
      let alive = true;
      page.appendChild(h('div.page-head', h('div', h('h1', 'Benchmarks'),
        h('p', 'Release-build measurements from the benchmark binaries on the Tricity graph. Same seeded query set for every algorithm, interleaved per pair. Wall-clock numbers vary between runs and machines; the ratios are what to compare.'))));

      (async () => {
        const results = await DL.api.results().catch(() => null);
        if (!alive) return;
        const B = results && results.benchmarks;
        if (!B || !B.routing) {
          page.appendChild(h('div.empty', h('h3', 'No benchmark results yet'), h('p', 'Run the benchmarks (or ./scripts/verify.sh) to produce benchmarks/results/*.json.'),
            DL.command('./build/benchmark_routing data/processed/tricity.graph --out benchmarks/results/routing.json')));
          return;
        }
        const R = B.routing, G = B.graph, D = B.dispatch, env = R.environment, ds = R.dataset;

        // Environment
        const envRows = [['Hardware', `${env.cpu} · ${env.logical_cores} cores · ${Math.round(env.memory_bytes / 1073741824)} GiB`], ['OS', env.os],
          ['Compiler', `${env.compiler} · ${env.build_type}`], ['Flags', h('code', `${env.cxx_flags} -std=c++20`)],
          ['Dataset', ds.source], ['Graph', `${fmt.int(ds.nodes)} nodes · ${fmt.int(ds.edges)} directed edges · v_max ${ds.max_speed_kmh} km/h`],
          ['Seed', `${R.seed} · ${fmt.int(R.queries)} queries + ${R.warmup} warm-up`], ['Measured', env.timestamp_utc.replace('T', ' ').replace('Z', ' UTC')]];
        page.appendChild(h('div.panel', h('div.panel-head', h('h2', 'Environment'), h('span.sub', 'Recorded with every result')),
          h('dl.rows', envRows.map(([k, v]) => h('div.row', h('dt', k), h('dd', v))))));

        // Routing latency
        const maxSettled = Math.max(...R.algorithms.map(a => a.settled_median));
        const base = R.algorithms[0].median_us;
        page.appendChild(h('section.section',
          h('div.section-head', h('div', h('h2', 'Routing latency'), h('p', 'Bar = median, ticks = p95 and p99, log scale. Includes path reconstruction.'))),
          h('div.grid.grid-3-1',
            h('div.panel', h('div.panel-body', DL.charts.latencyRanges(R.algorithms.map(a => ({ label: a.name, color: algoMeta(a.name).color, median: a.median_us, p95: a.p95_us, p99: a.p99_us })), { label: 'Routing latency percentiles' }))),
            h('div.panel', h('div.panel-head', h('h2', 'Search effort'), h('span.sub', 'Nodes settled, median')),
              h('div.panel-body', { style: { display: 'grid', gap: '12px' } }, R.algorithms.map(a => h('div',
                h('div', { style: { display: 'flex', justifyContent: 'space-between', fontSize: '12px', marginBottom: '6px' } }, DL.algoTag(algoMeta(a.name)), h('span.num.muted', fmt.int(a.settled_median))),
                h('div.bar-track', h('div.bar-fill', { style: { width: `${Math.max(0.8, (a.settled_median / maxSettled) * 100)}%`, background: algoMeta(a.name).color, opacity: algoMeta(a.name).variant ? 0.5 : null } }))))))),
          h('div.panel', { style: { marginTop: '16px' } }, h('div.table-wrap', h('table.data',
            h('thead', h('tr', h('th', 'Algorithm'), h('th.r', 'Median'), h('th.r', 'p95'), h('th.r', 'p99'), h('th.r', 'Mean'), h('th.r', 'Settled (median)'), h('th.r', 'Settled (mean)'), h('th.r', 'Workspace'), h('th.r', 'Median vs Dijkstra'))),
            h('tbody', R.algorithms.map(a => h('tr', h('td', DL.algoTag(algoMeta(a.name))), h('td.r', fmt.us(a.median_us)), h('td.r', fmt.us(a.p95_us)), h('td.r', fmt.us(a.p99_us)),
              h('td.r', fmt.us(a.mean_us)), h('td.r', fmt.int(a.settled_median)), h('td.r', fmt.int(a.settled_mean)), h('td.r', fmt.bytes(a.memory_bytes)), h('td.r', `${(base / a.median_us).toFixed(2)}×`)))))),
            h('div.panel-foot', `Shared graph: ${fmt.bytes(R.graph_memory_bytes)}, excluded from per-algorithm workspace. A* uses the chord heuristic (precomputed unit vectors). A* (haversine) is the original heuristic: its tails are slower than Dijkstra's because each push computes a haversine, and v_max = 60 km/h makes the bound loose on 20 km/h residential streets.`))));

        // Distance buckets
        const buckets = R.buckets;
        const keys = [['dijkstra', 'Dijkstra'], ['bidirectional', 'Bidirectional'], ['astar', 'A*'], ['cch', 'CCH']].filter(([k]) => buckets[0][`${k}_median_us`] !== undefined);
        page.appendChild(h('section.section',
          h('div.section-head', h('div', h('h2', 'Latency by trip length'), h('p', 'Median latency per travel-time bucket (Dijkstra cost), log scale.'))),
          h('div.panel', h('div.panel-head', DL.charts.legend(keys.map(([k, l]) => ({ label: l, color: `var(--s-${k})` }))), h('span.sub', buckets.map(b => `${b.label}: ${b.count}`).join(' · '))),
            h('div.panel-body', DL.charts.lineChart(keys.map(([k, l]) => ({ name: l, color: `var(--s-${k})`, points: buckets.map((b, i) => [i, b[`${k}_median_us`]]) })),
              { log: true, dots: true, xTicks: buckets.map((_, i) => i), xFormat: i => buckets[i] ? buckets[i].label : '', yFormat: v => fmt.us(v), width: 1160, height: 300, label: 'Median latency by trip length' })))));

        // CCH preprocessing
        if (R.cch) {
          const c = R.cch, total = c.order_ms + c.contract_ms + c.customize_ms;
          const parts = [['Ordering', c.order_ms, 'var(--text-3)'], ['Contraction', c.contract_ms, 'var(--s-astar)'], ['Customization', c.customize_ms, 'var(--s-cch)']];
          page.appendChild(h('section.section',
            h('div.section-head', h('div', h('h2', 'CCH preprocessing'), h('p', 'One-time cost, measured separately from queries. Re-customizing for new weights (a traffic scenario) reuses the topology.'))),
            h('div.panel', h('div.panel-body',
              h('div', { style: { display: 'flex', height: '14px', borderRadius: '4px', overflow: 'hidden', gap: '2px' } },
                parts.map(([l, v, col]) => h('div', { title: `${l}: ${v.toFixed(0)} ms`, style: { width: `${(v / total) * 100}%`, background: col } }))),
              h('div.chart-legend', { style: { marginTop: '10px' } }, parts.map(([l, v, col]) => h('span', h('span.swatch', { style: { background: col } }), `${l} ${v.toFixed(0)} ms`)))),
              h('dl.facts',
                h('div.fact', h('dt', 'Total preprocessing'), h('dd', fmt.num(c.preprocessing_ms, 0), h('small', 'ms'))),
                h('div.fact', h('dt', 'Re-customization'), h('dd', fmt.num(c.recustomize_ms, 0), h('small', 'ms'))),
                h('div.fact', h('dt', 'Upward edges'), h('dd', fmt.int(c.upward_edges), h('small', `${(c.upward_edges / c.original_pairs).toFixed(1)}× fill-in`))),
                h('div.fact', h('dt', 'Hierarchy'), h('dd', fmt.bytes(c.hierarchy_bytes))),
                h('div.fact', h('dt', 'Break-even'), h('dd', `≈${Math.round(c.preprocessing_ms * 1000 / (R.algorithms[0].median_us - R.algorithms[3].median_us))}`, h('small', 'Dijkstra queries')))),
              h('div.panel-foot', c.ordering))));
        }

        // Graph representation
        if (G) {
          const rows = [['Memory (forward topology + weights)', G.csr.memory_bytes, G.adjacency.memory_bytes, fmt.bytes],
            ['Load from binary file', G.csr.load_ms, G.adjacency.load_ms, v => `${v.toFixed(1)} ms`],
            ['Construction from edge list', G.csr.build_ms, G.adjacency.build_ms, v => `${v.toFixed(1)} ms`],
            ['Sequential traversal (per edge)', G.csr.traversal_ns_per_edge, G.adjacency.traversal_ns_per_edge, v => `${v.toFixed(2)} ns`],
            ['Dijkstra query (median)', G.csr.dijkstra_median_us, G.adjacency.dijkstra_median_us, fmt.us],
            ['Dijkstra query (p95)', G.csr.dijkstra_p95_us, G.adjacency.dijkstra_p95_us, fmt.us]];
          page.appendChild(h('section.section',
            h('div.section-head', h('div', h('h2', 'CSR vs adjacency list'), h('p', 'Same graph, same queries. CSR load also builds the reverse CSR and validates the graph.'))),
            h('div.panel', h('div.table-wrap', h('table.data',
              h('thead', h('tr', h('th', 'Metric'), h('th.r', 'CSR'), h('th.r', 'Adjacency list'), h('th', 'Adjacency ÷ CSR'))),
              h('tbody', rows.map(([l, c, a, f]) => {
                const ratio = a / c;
                return h('tr', h('td', l), h('td.r', f(c)), h('td.r', f(a)),
                  h('td', h('div.bar-cell', h('div.bar-track', h('div.bar-fill', { style: { width: `${Math.min(100, (ratio / 3) * 100)}%`, background: 'var(--text)' } })), h('span.num', { style: { minWidth: '44px', textAlign: 'right' } }, `${ratio.toFixed(2)}×`))));
              })))),
              h('div.panel-foot', `Full Graph object (forward + reverse CSR, lengths, classes, coordinates, OSM ids): ${fmt.bytes(G.full_graph_memory_bytes)}. Construction runs on an already-sorted edge list, the best case for sorting.`))));
        }

        // Dispatch latency
        if (D) {
          const counts = [...new Set(D.results.map(r => r.couriers))];
          page.appendChild(h('section.section',
            h('div.section-head', h('div', h('h2', 'Dispatch decision latency'), h('p', 'One choose() call with every courier available. Greedy ETA runs one reverse Dijkstra plus one A* per decision.'))),
            h('div.panel', h('div.table-wrap', h('table.data',
              h('thead', h('tr', h('th', 'Policy'), ...counts.map(n => h('th.r', `${n} couriers (median)`)), h('th.r', 'p95 at 500'))),
              h('tbody', DL.POLICIES.map(p => h('tr', h('td', DL.algoTag(p)),
                ...counts.map(n => h('td.r', fmt.us(D.results.find(r => r.policy === p.id && r.couriers === n).median_us))),
                h('td.r', fmt.us(D.results.find(r => r.policy === p.id && r.couriers === counts[counts.length - 1]).p95_us))))))),
              h('div.panel-foot', `${fmt.int(D.samples)} samples per cell. Greedy ETA's cost barely grows with couriers because its reverse search settles most of the city regardless.`))));
        }

        // Greedy ETA strategy study
        const E = results.eta;
        if (E && E.tricity_uniform) {
          const sec = h('section.section');
          page.appendChild(sec);
          const body = h('div');
          let demand = 'tricity_uniform';
          const toggle = DL.segmented([{ value: 'tricity_uniform', label: 'Uniform demand' }, { value: 'tricity_clustered', label: 'Clustered demand' }].filter(o => E[o.value]),
            demand, v => { demand = v; renderEta(); }, { label: 'Demand model' });
          sec.append(h('div.section-head',
            h('div', h('h2', 'Greedy ETA: evaluating candidates'), h('p', 'Greedy ETA needs every available courier’s road time to the restaurant. Each strategy below gets there with less work and must reach exactly the same decisions. The decision latency above is the full baseline.')),
            toggle), body);

          const renderEta = () => {
            const run = E[demand], S = summarizeEta(run);
            const strategies = STRATEGIES.filter(s => run.strategies.includes(s.id));
            const best = {};
            for (const size of S.sizes) best[size] = Math.min(...strategies.map(s => S.mean(size, s.id) ?? Infinity));
            // Geometric mean of the speed-up over full across scenarios.
            const vsFull = s => Math.exp(S.sizes.reduce((acc, size) => acc + Math.log(S.mean(size, 'full') / S.mean(size, s)), 0) / S.sizes.length);
            const exact = S.identical === S.runs;
            body.replaceChildren(h('div.fade-in',
              h('div', { style: { display: 'flex', gap: '8px', flexWrap: 'wrap', alignItems: 'center', marginBottom: '12px', fontSize: '13px' } },
                exact ? h('span.badge.ok', icon('check'), `Identical decisions in ${S.runs} of ${S.runs} paired runs`) : h('span.badge.err', icon('x'), `${S.runs - S.identical} runs diverged`),
                h('span.muted', `Full simulations · moderate traffic · ${S.seeds} seeds per cell · mean time per dispatch decision`)),
              h('div.panel', h('div.table-wrap', h('table.data.eta-table',
                h('thead', h('tr', h('th', 'Strategy'),
                  ...S.sizes.map(size => { const c = S.cells[`${size}|full`]; return h('th.r', h('div', SIZE_LABEL[size] || size), h('div.faint', { style: { fontWeight: 400 } }, `${c.couriers} / ${fmt.int(c.orders)}`)); }),
                  h('th.r', 'vs full'))),
                h('tbody', strategies.map(s => h('tr',
                  h('td', h('div.mono', s.label), h('div.faint', { style: { fontSize: '12px', whiteSpace: 'normal', maxWidth: '34ch' } }, s.note)),
                  ...S.sizes.map(size => { const v = S.mean(size, s.id); return h('td.r', { class: v === best[size] ? 'r eta-best' : 'r' }, v === null ? '–' : fmt.us(v)); }),
                  h('td.r', s.id === 'full' ? '1.0×' : `${vsFull(s.id).toFixed(1)}×`)))))),
                h('div.panel-foot', `Columns are couriers / orders; peak runs fit the same orders into half the window. Highlighted cells are the fastest strategy for that scenario. “vs full” is the geometric mean speed-up across the scenarios. Seed-to-seed range is up to ${Math.round(S.spread * 100)}% of a cell’s mean, so treat gaps under ~1.2× as ties.`))));
          };
          renderEta();

          const N = E.northern;
          if (N && N.micro && N.micro.length) {
            const regime = N.micro.some(m => m.regime === 'not_ready') ? 'not_ready' : N.micro[0].regime;
            const rows = N.micro.filter(m => m.regime === regime);
            const counts = [...new Set(rows.map(m => m.couriers))];
            const strategies = STRATEGIES.filter(s => N.strategies.includes(s.id));
            const cell = (s, n) => rows.find(m => m.strategy === s && m.couriers === n);
            const best = Object.fromEntries(counts.map(n => [n, Math.min(...strategies.map(s => cell(s.id, n)?.median_us ?? Infinity))]));
            sec.appendChild(h('div.panel', { style: { marginTop: '16px' } },
              h('div.panel-head', h('h2', 'Scale check: northern India'), h('span.sub', `${fmt.int(N.dataset.nodes)} nodes · one decision, food not ready`)),
              h('div.table-wrap', h('table.data',
                h('thead', h('tr', h('th', 'Strategy'), ...counts.map(n => h('th.r', `${n} couriers`)), h('th.r', 'Exact distances at 500'))),
                h('tbody', strategies.map(s => h('tr', h('td.mono', s.label),
                  ...counts.map(n => { const m = cell(s.id, n); return h('td.r', { class: m && m.median_us === best[n] ? 'r eta-best' : 'r' }, m ? fmt.us(m.median_us) : '–'); }),
                  h('td.r', (() => { const m = cell(s.id, counts[counts.length - 1]); return m ? fmt.int(m.exact_median) : '–'; })())))))),
              h('div.panel-foot', 'Median of single decisions with all couriers at random nodes. Out of sample: the 20,000-node switch was chosen on Tricity before this graph was loaded. p95 is dominated by long trips across the region for every strategy; see docs/RESEARCH.md §7–8.')));
          }
        }
      })().catch(e => { if (alive) page.appendChild(DL.errorCallout(e)); });

      return () => { alive = false; };
    },
  };
})();
