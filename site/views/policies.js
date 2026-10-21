// Policies: Nearest vs Greedy ETA on one shared scenario (live), and the
// paired-seed experiment results from the experiment runner.
(function () {
  'use strict';
  const DL = window.DL;
  const { h, icon, fmt } = DL;
  DL.views = DL.views || {};
  DL.state = DL.state || {};
  const S = (DL.state.policies = DL.state.policies || {
    params: { couriers: 40, orders: 300, window: 120, seed: 7, traffic: 'moderate' },
    compare: null, set: 'verify', cell: null,
  });

  const METRICS = [
    { key: 'orders_delivered', label: 'Completed orders', f: fmt.int, better: 'higher' },
    { key: 'orders_undelivered', label: 'Undelivered at end', f: fmt.int, better: 'lower' },
    { key: 'avg_delivery_min', label: 'Average delivery', f: fmt.min, better: 'lower', d: 2 },
    { key: 'p95_delivery_min', label: 'p95 delivery', f: fmt.min, better: 'lower', d: 2 },
    { key: 'avg_wait_for_courier_min', label: 'Average wait for a courier', f: fmt.min, better: 'lower', d: 2 },
    { key: 'courier_utilization', label: 'Courier utilization', f: fmt.pct, better: null },
    { key: 'total_distance_km', label: 'Total travel', f: v => `${fmt.num(v, 1)} km`, better: 'lower', d: 1 },
    { key: 'assign_latency_mean_us', label: 'Decision latency (mean)', f: fmt.us, better: 'lower', wall: true },
  ];

  DL.views.policies = {
    mount(root) {
      const page = h('div.page.fade-in');
      root.appendChild(page);
      let alive = true;

      page.appendChild(h('div.page-head', h('div', h('h1', 'Dispatch policies'),
        h('p', 'Nearest picks the available courier closest to the restaurant as the crow flies. Greedy ETA picks the one with the earliest estimated delivery, using road travel times from the engine. Every comparison runs both policies on one identical, pre-generated scenario.'))));

      // ---- live head-to-head
      const live = h('section.section', { style: { marginTop: 0 } });
      page.appendChild(live);
      live.appendChild(h('div.section-head', h('div', h('h2', 'Head-to-head'), h('p', 'One scenario, both policies, same seed, same traffic, same courier starts.'))));
      const out = h('div');
      if (DL.api.live) {
        const inputs = {
          couriers: DL.numberInput(S.params.couriers, 1, 1000), orders: DL.numberInput(S.params.orders, 1, 5000),
          window: DL.numberInput(S.params.window, 10, 1440), seed: DL.numberInput(S.params.seed, 0, 2147483647),
        };
        const traffic = h('select.input', ['free', 'moderate', 'heavy'].map(t => h('option', { value: t, selected: t === S.params.traffic ? true : null }, t[0].toUpperCase() + t.slice(1))));
        const btn = h('button.btn.primary', { type: 'button' }, icon('policies'), 'Compare policies');
        DL.demoLock(inputs.couriers, inputs.orders, inputs.window, inputs.seed);
        live.appendChild(h('div.panel', h('div.panel-body', h('div.form-row',
          DL.field('Couriers', inputs.couriers), DL.field('Orders', inputs.orders), DL.field('Window (min)', inputs.window),
          DL.field('Seed', inputs.seed), DL.field('Traffic', traffic), h('div', { style: { marginLeft: 'auto' } }, btn)))));
        btn.addEventListener('click', async () => {
          const p = {
            couriers: DL.readInt(inputs.couriers, 1, 1000), orders: DL.readInt(inputs.orders, 1, 5000),
            window: DL.readInt(inputs.window, 10, 1440), seed: DL.readInt(inputs.seed, 0, 2147483647),
          };
          if (Object.values(p).some(v => v === null)) { out.replaceChildren(DL.errorCallout('Check the highlighted fields.')); return; }
          Object.assign(S.params, p, { traffic: traffic.value });
          btn.classList.add('loading');
          btn.disabled = true;
          out.replaceChildren(h('div.grid.grid-2', { style: { marginTop: '16px' } }, h('div.skeleton', { style: { height: '320px' } }), h('div.skeleton', { style: { height: '320px' } })));
          try {
            S.compare = await DL.api.get('/api/compare', { ...S.params });
            if (alive) renderCompare();
          } catch (e) {
            if (alive) out.replaceChildren(h('div', { style: { marginTop: '16px' } }, DL.errorCallout(e)));
          } finally {
            btn.classList.remove('loading');
            btn.disabled = false;
          }
        });
        live.appendChild(out);
        if (S.compare) renderCompare();
        else out.appendChild(h('p.muted', { style: { marginTop: '12px', fontSize: '13px' } }, 'The defaults are a peak-load scenario (300 orders in two hours), where the two policies are most likely to differ.'));
      } else {
        live.appendChild(DL.serverRequired('Head-to-head runs need the engine', 'Each comparison simulates both policies in C++ on the same scenario. The experiment results below come from the result files and work offline.'));
      }

      function renderCompare() {
        const c = S.compare;
        const [a, b] = c.runs;
        const rows = METRICS.map(m => {
          const va = a.metrics[m.key], vb = b.metrics[m.key], d = vb - va;
          let cls = 'r delta-flat';
          if (m.better && Math.abs(d) > 1e-9) cls = (m.better === 'lower' ? d < 0 : d > 0) ? 'r delta-good' : 'r delta-bad';
          const dtxt = m.key === 'courier_utilization' ? `${fmt.signed(d * 100, 1)} pt` : m.wall ? `${fmt.int(vb / Math.max(va, 1e-9))}× slower` : fmt.signed(d, m.d ?? 0);
          return h('tr', h('td', m.label, m.wall ? h('span.faint', ' · wall clock') : null), h('td.r', m.f(va)), h('td.r', m.f(vb)), h('td', { class: cls }, dtxt));
        });
        const chartCum = DL.charts.lineChart(c.runs.map((r, i) => ({
          name: DL.POLICIES[i].label, color: DL.POLICIES[i].color,
          points: [[0, 0], ...r.delivered_at_min.map((t, k) => [t, k + 1])],
        })), { step: true, xFormat: v => `${v}m`, height: 220, label: 'Cumulative deliveries over time' });
        const hist = DL.charts.histogram(c.runs.map((r, i) => ({ name: DL.POLICIES[i].label, color: DL.POLICIES[i].color, values: r.delivery_min })),
          { xFormat: v => `${Math.round(v)}`, xLabel: 'minutes from order to door', height: 200, label: 'Distribution of delivery times' });
        out.replaceChildren(h('div.fade-in', { style: { marginTop: '16px' } },
          h('div', { style: { display: 'flex', gap: '8px', flexWrap: 'wrap', alignItems: 'center', marginBottom: '12px', fontSize: '13px' } },
            h('span.badge.ok', icon('check'), 'Same scenario'), h('span.hash', c.scenario.scenario_hash),
            h('span.muted', `${c.scenario.couriers} couriers · ${fmt.int(c.scenario.orders)} orders over ${c.scenario.window_min} min · ${c.scenario.traffic} traffic · seed ${c.scenario.seed}`)),
          h('div.grid.grid-2',
            h('div.panel', h('div.table-wrap', h('table.data',
              h('thead', h('tr', h('th', 'Metric'), h('th.r', DL.algoTag(DL.POLICIES[0])), h('th.r', DL.algoTag(DL.POLICIES[1])), h('th', 'Greedy ETA vs Nearest'))),
              h('tbody', rows))),
              h('div.panel-foot', `Final state hashes: ${a.final_state_hash} · ${b.final_state_hash}. Green marks the better value; utilization has no better direction.`)),
            h('div.panel', h('div.panel-head', h('h2', 'Deliveries over time'), DL.charts.legend(DL.POLICIES)),
              h('div.panel-body', chartCum),
              h('div.panel-head', { style: { borderTop: '1px solid var(--border)' } }, h('h2', 'Delivery time distribution')),
              h('div.panel-body', hist)))));
      }

      // ---- experiment results
      const exSection = h('section.section');
      page.appendChild(exSection);
      (async () => {
        const results = await DL.api.results().catch(() => null);
        if (!alive) return;
        const sets = results && results.experiments ? Object.entries(results.experiments).filter(([, v]) => v) : [];
        exSection.appendChild(h('div.section-head', h('div', h('h2', 'Experiment results'),
          h('p', 'From the experiment runner: every cell is one pre-generated scenario per seed, run under both policies.'))));
        if (!sets.length) {
          exSection.appendChild(h('div.empty', h('h3', 'No experiment results yet'), h('p', 'Run the experiment runner to produce them.'),
            DL.command('./build/dispatchlab experiment data/processed/tricity.graph --config experiments/configs/verify.json --out experiments/results')));
          return;
        }
        if (!sets.find(([k]) => k === S.set)) S.set = sets[0][0];
        const body = h('div');
        if (sets.length > 1) {
          exSection.querySelector('.section-head').appendChild(DL.segmented(sets.map(([k, v]) => ({ value: k, label: k === 'full' ? 'Full set' : 'Verify set', title: v.experiment_id })),
            S.set, v => { S.set = v; S.cell = null; renderSet(); }, { label: 'Result set' }));
        }
        exSection.appendChild(body);
        function renderSet() {
          const ex = results.experiments[S.set];
          const cells = DL.policyCells(ex);
          const sizes = [...new Set(cells.map(c => c.size))], traffic = [...new Set(cells.map(c => c.traffic))];
          if (!S.cell || !cells.find(c => `${c.size}|${c.traffic}` === S.cell)) S.cell = `${cells[0].size}|${cells[0].traffic}`;
          const heat = h('div.heat', { style: { gridTemplateColumns: `minmax(150px, 1.3fr) repeat(${traffic.length}, minmax(0, 1fr))` } },
            h('div.h', 'Average delivery, Greedy ETA − Nearest'), traffic.map(t => h('div.h', `${t} traffic`)));
          for (const s of sizes) {
            const first = cells.find(c => c.size === s);
            heat.appendChild(h('div', h('div', { style: { fontWeight: 500 } }, s.replace('_', ' ')), h('div.faint', `${first.couriers} couriers · ${fmt.int(first.orders)} orders`)));
            for (const t of traffic) {
              const c = cells.find(x => x.size === s && x.traffic === t);
              const mag = Math.min(1, Math.abs(c.delta) / 2);
              const tone = c.delta < 0 ? 'var(--ok)' : 'var(--err)';
              const btn = h('button.cell', { type: 'button', 'aria-pressed': String(S.cell === `${s}|${t}`), style: { background: `color-mix(in srgb, ${tone} ${Math.round(4 + mag * 22)}%, var(--surface))` } },
                h('span.v', `${fmt.signed(c.delta)} min`), h('span.w', `ETA faster in ${c.wins}/${c.pairs.length} seeds`));
              btn.addEventListener('click', () => { S.cell = `${s}|${t}`; renderSet(); });
              heat.appendChild(btn);
            }
          }
          const cell = cells.find(c => `${c.size}|${c.traffic}` === S.cell);
          const detail = h('div.panel', { style: { marginTop: '16px' } },
            h('div.panel-head', h('h2', `${cell.size.replace('_', ' ')} · ${cell.traffic} traffic`), h('span.sub', `${cell.pairs.length} paired seeds`)),
            h('div.table-wrap', h('table.data',
              h('thead', h('tr', h('th', 'Seed'), h('th.r', 'Nearest avg'), h('th.r', 'Greedy ETA avg'), h('th.r', 'Δ'), h('th.r', 'Undelivered N / E'), h('th.r', 'Travel N / E'), h('th', 'Scenario hash'))),
              h('tbody', cell.pairs.map(p => {
                const d = p.greedy_eta.avg_delivery_min - p.nearest.avg_delivery_min;
                return h('tr', h('td.num', p.nearest.seed), h('td.r', fmt.min(p.nearest.avg_delivery_min)), h('td.r', fmt.min(p.greedy_eta.avg_delivery_min)),
                  h('td', { class: d < -0.005 ? 'r delta-good' : d > 0.005 ? 'r delta-bad' : 'r delta-flat' }, fmt.signed(d)),
                  h('td.r', `${p.nearest.undelivered} / ${p.greedy_eta.undelivered}`),
                  h('td.r', `${fmt.int(p.nearest.total_distance_km)} / ${fmt.int(p.greedy_eta.total_distance_km)} km`),
                  h('td', h('span.hash', p.nearest.scenario_hash), p.nearest.scenario_hash === p.greedy_eta.scenario_hash ? null : h('span.badge.err', 'differs')));
              })))),
            h('div.panel-foot', `${ex.experiment_id} · routing ${ex.routing_algorithm} · ${ex.environment.cpu} · generated ${ex.generated_utc.replace('T', ' ').replace('Z', ' UTC')}. Both policies share each row's scenario hash.`));
          body.replaceChildren(h('div.heat-scroll', heat), detail);
        }
        renderSet();
      })().catch(e => { if (alive) exSection.appendChild(DL.errorCallout(e)); });

      return () => { alive = false; };
    },
  };
})();
