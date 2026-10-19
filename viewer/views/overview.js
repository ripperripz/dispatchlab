// Overview: the dataset, the engine and the headline measured results.
(function () {
  'use strict';
  const DL = window.DL;
  const { h, icon, fmt } = DL;
  DL.views = DL.views || {};

  // Shared empty state for features that need the live engine.
  DL.serverRequired = function (title, body) {
    return h('div.empty.fade-in',
      h('span.badge.warn', icon('bolt'), 'Needs the live engine'),
      h('h3', title),
      h('p', body),
      DL.command('./build/dispatchlab serve data/processed/tricity.graph'),
      h('p.faint', 'Then open http://127.0.0.1:8080 in your browser. The server binds to the loopback interface only.'));
  };

  DL.views.overview = {
    mount(root) {
      const page = h('div.page.fade-in');
      root.appendChild(page);
      let map = null;
      let alive = true;

      page.appendChild(h('div.page-head',
        h('div', h('h1', 'Tricity road network'),
          h('p', 'Chandigarh, Mohali and Panchkula from OpenStreetMap (extract of 17 Oct 2026). The engine keeps the largest strongly connected component of the drivable roads. Everything on this console is computed by the engine or read from its own result files.')),
        h('div.form-row',
          h('a.btn', { href: '#/simulation' }, icon('simulation'), 'Run a simulation'),
          h('a.btn.primary', { href: '#/routing' }, icon('route'), 'Open routing lab'))));

      const grid = h('div.grid.grid-3-1.stretch');
      page.appendChild(grid);
      const mapPanel = h('section.panel.column',
        h('div.panel-head', h('h2', 'Road network'), h('span.sub', matchMedia('(pointer: coarse)').matches ? 'Drag to pan · pinch to zoom' : 'Drag to pan · scroll to zoom')));
      const host = h('div.map-host', { style: { background: 'var(--map-bg)' } });
      mapPanel.appendChild(host);
      const facts = h('dl.facts');
      mapPanel.appendChild(facts);
      grid.appendChild(mapPanel);
      const side = h('div.grid', { style: { alignContent: 'start' } });
      grid.appendChild(side);

      const routingSection = h('section.section');
      const dispatchSection = h('section.section');
      page.appendChild(routingSection);
      page.appendChild(dispatchSection);

      const fact = (label, value, unit) => h('div.fact', h('dt', label), h('dd', value, unit ? h('small', unit) : null));

      (async () => {
        const [net, results] = await Promise.all([DL.api.network(), DL.api.results().catch(() => null)]);
        if (!alive) return;
        const info = DL.api.info;
        const ds = results && results.benchmarks && results.benchmarks.routing ? results.benchmarks.routing.dataset : null;

        if (net) {
          const canvas = h('canvas.map-canvas', { 'aria-label': 'Map of the Tricity road network' });
          host.appendChild(canvas);
          map = new DL.MapView(canvas, net, { padding: 16 });
          host.appendChild(DL.MapView.tools(map));
          host.appendChild(h('div.map-legend',
            h('span', h('span.legend-line', { style: { background: 'var(--map-arterial)', height: '3px' } }), 'Arterial (trunk, primary, secondary)'),
            h('span', h('span.legend-line', { style: { background: 'var(--map-road)' } }), 'Local roads')));
        } else {
          host.appendChild(h('div.panel-body', DL.serverRequired('No network loaded',
            'Start the engine, or export a recorded replay with `dispatchlab export-viewer --js viewer/replay_data.js`.')));
        }

        const nodes = info ? info.graph.nodes : ds ? ds.nodes : net ? net.n : null;
        const edges = info ? info.graph.edges : ds ? ds.edges : null;
        const vmax = info ? info.graph.max_speed_kmh : ds ? ds.max_speed_kmh : null;
        facts.append(
          fact('Nodes', fmt.int(nodes)),
          fact('Directed edges', fmt.int(edges)),
          fact('Road length', info ? fmt.int(info.graph.total_length_km) : '–', info ? 'km' : null),
          fact('v_max (A* bound)', vmax ? vmax : '–', vmax ? 'km/h' : null),
          fact('Strongly connected', info ? (info.graph.strongly_connected ? 'Yes' : 'No') : '–'));

        // Engine panel.
        const eng = h('section.panel', h('div.panel-head', h('h2', 'Engine'), DL.api.live ? h('span.badge.ok', icon('check'), 'Live') : h('span.badge', 'Recorded')));
        const rows = h('dl.rows');
        const row = (k, v) => rows.appendChild(h('div.row', h('dt', k), h('dd', v)));
        if (info) {
          row('Graph in memory', fmt.bytes(info.graph.memory_bytes));
          row('Validation', info.graph.validate === 'ok' ? 'All checks pass' : info.graph.validate);
          row('CCH upward edges', `${fmt.int(info.cch.upward_edges)} (${(info.cch.upward_edges / info.cch.original_pairs).toFixed(1)}× fill-in)`);
          row('CCH preprocessing (this server start)', `${fmt.num(info.cch.order_ms + info.cch.contract_ms + info.cch.customize_ms, 0)} ms`);
          row('Hierarchy size', fmt.bytes(info.cch.hierarchy_bytes));
          row('Compiler', info.environment.compiler);
          row('Processor', info.environment.cpu);
        } else if (ds) {
          const env = results.benchmarks.routing.environment;
          row('Graph source', 'Recorded benchmark run');
          row('Compiler', env.compiler);
          row('Processor', env.cpu);
          row('Measured at', env.timestamp_utc.replace('T', ' ').replace('Z', ' UTC'));
        } else {
          row('Status', 'No engine connected');
        }
        eng.appendChild(rows);
        side.appendChild(eng);

        if (info) {
          const classes = info.graph.classes.slice().sort((a, b) => b.edges - a.edges);
          const max = Math.max(...classes.map(c => c.edges));
          const list = h('div.panel-body', { style: { display: 'grid', gap: '10px' } },
            classes.map(c => h('div', { style: { display: 'grid', gridTemplateColumns: '112px 1fr 64px', alignItems: 'center', gap: '10px', fontSize: '12px' } },
              h('span', { title: `${c.speed_kmh} km/h default speed` }, c.class.replace('_', ' ')),
              h('div.bar-track', h('div.bar-fill', { style: { width: `${(c.edges / max) * 100}%`, background: c.arterial ? 'var(--text)' : 'var(--text-3)' } })),
              h('span.num', { style: { textAlign: 'right', color: 'var(--text-2)' } }, fmt.int(c.edges)))));
          side.appendChild(h('section.panel', h('div.panel-head', h('h2', 'Road classes'), h('span.sub', 'Directed edges')), list,
            h('div.panel-foot', 'Speeds are documented class defaults (docs/DATA.md), not measurements.')));
        }

        // Routing summary from the benchmark results.
        const rb = results && results.benchmarks && results.benchmarks.routing;
        routingSection.appendChild(h('div.section-head',
          h('div', h('h2', 'Routing performance'), h('p', rb ? `benchmark_routing · ${fmt.int(rb.queries)} seeded queries · same pairs for every algorithm` : 'No benchmark results found yet.')),
          h('a.btn.sm', { href: '#/benchmarks' }, 'All benchmarks')));
        if (rb) {
          const base = rb.algorithms[0].median_us;
          const maxSettled = Math.max(...rb.algorithms.map(a => a.settled_median));
          const byName = { Dijkstra: 'dijkstra', Bidirectional: 'bidirectional', 'A*': 'astar', CCH: 'cch' };
          routingSection.appendChild(h('div.panel', h('div.table-wrap', h('table.data',
            h('thead', h('tr', h('th', 'Algorithm'), h('th.r', 'Median'), h('th.r', 'p95'), h('th.r', 'p99'), h('th', 'Nodes settled (median)'), h('th.r', 'vs Dijkstra'))),
            h('tbody', rb.algorithms.map(a => {
              const meta = DL.ALGOS.find(x => x.id === byName[a.name]) || DL.ALGOS[0];
              return h('tr',
                h('td', DL.algoTag(meta)),
                h('td.r', fmt.us(a.median_us)), h('td.r', fmt.us(a.p95_us)), h('td.r', fmt.us(a.p99_us)),
                h('td', h('div.bar-cell', h('div.bar-track', h('div.bar-fill', { style: { width: `${(a.settled_median / maxSettled) * 100}%`, background: meta.color } })), h('span.num', { style: { minWidth: '64px', textAlign: 'right' } }, fmt.int(a.settled_median)))),
                h('td.r', `${(base / a.median_us).toFixed(1)}×`));
            })))),
            h('div.panel-foot', `Measured ${rb.environment.timestamp_utc.replace('T', ' ').replace('Z', ' UTC')} on ${rb.environment.cpu}, ${rb.environment.compiler}, ${rb.environment.build_type}. All four algorithms return identical costs on every query.`)));
        }

        // Dispatch summary from the experiment runner.
        const ex = results && results.experiments && results.experiments.verify;
        dispatchSection.appendChild(h('div.section-head',
          h('div', h('h2', 'Dispatch policies'), h('p', ex ? `${ex.experiment_id} · ${ex.seeds.length} paired seeds per cell · one shared scenario per cell` : 'No experiment results found yet.')),
          h('a.btn.sm', { href: '#/policies' }, 'Compare policies')));
        if (ex) dispatchSection.appendChild(DL.policySummaryTable(ex));
      })().catch(e => { if (alive) page.appendChild(DL.errorCallout(e)); });

      return () => { alive = false; if (map) map.destroy(); };
    },
  };

  // size × traffic → mean avg-delivery for both policies, paired delta, wins.
  DL.policyCells = function (ex) {
    const cells = new Map();
    for (const r of ex.results) {
      const key = `${r.size}|${r.traffic}`;
      if (!cells.has(key)) cells.set(key, { size: r.size, traffic: r.traffic, couriers: r.couriers, orders: r.orders, seeds: new Map() });
      const c = cells.get(key);
      if (!c.seeds.has(r.seed)) c.seeds.set(r.seed, {});
      c.seeds.get(r.seed)[r.policy] = r;
    }
    for (const c of cells.values()) {
      const pairs = [...c.seeds.values()].filter(p => p.nearest && p.greedy_eta);
      const avg = (f) => pairs.reduce((s, p) => s + f(p), 0) / (pairs.length || 1);
      c.pairs = pairs;
      c.nearest = avg(p => p.nearest.avg_delivery_min);
      c.eta = avg(p => p.greedy_eta.avg_delivery_min);
      c.delta = avg(p => p.greedy_eta.avg_delivery_min - p.nearest.avg_delivery_min);
      c.wins = pairs.filter(p => p.greedy_eta.avg_delivery_min < p.nearest.avg_delivery_min).length;
      c.undelNearest = avg(p => p.nearest.undelivered);
      c.undelEta = avg(p => p.greedy_eta.undelivered);
    }
    return [...cells.values()];
  };

  DL.policySummaryTable = function (ex) {
    const cells = DL.policyCells(ex);
    return h('div.panel', h('div.table-wrap', h('table.data.nowrap',
      h('thead', h('tr', h('th', 'Scenario'), h('th', 'Traffic'), h('th.r', 'Nearest avg'), h('th.r', 'Greedy ETA avg'), h('th.r', 'Δ (paired)'), h('th.r', 'ETA better'), h('th.r', 'Undelivered N / E'))),
      h('tbody', cells.map(c => h('tr',
        h('td', h('span', { style: { fontWeight: 500 } }, c.size.replace('_', ' ')), h('span.cell-sub', `${c.couriers} couriers · ${fmt.int(c.orders)} orders`)),
        h('td', c.traffic),
        h('td.r', fmt.min(c.nearest)), h('td.r', fmt.min(c.eta)),
        h('td.r', { class: c.delta < -0.005 ? 'r delta-good' : c.delta > 0.005 ? 'r delta-bad' : 'r delta-flat' }, `${fmt.signed(c.delta)} min`),
        h('td.r', `${c.wins}/${c.pairs.length}`),
        h('td.r', `${fmt.num(c.undelNearest, 1)} / ${fmt.num(c.undelEta, 1)}`)))))),
      h('div.panel-foot', 'Negative Δ means Greedy ETA delivered faster on average. Average delivery counts delivered orders only, so read it together with undelivered orders.'));
  };
})();
