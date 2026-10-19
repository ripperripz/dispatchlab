// Correctness: run the engine's own checks live — algorithm agreement,
// determinism with replay, and rejection of corrupted event logs.
(function () {
  'use strict';
  const DL = window.DL;
  const { h, icon, fmt } = DL;
  DL.views = DL.views || {};
  DL.state = DL.state || {};
  const S = (DL.state.correctness = DL.state.correctness || { agreement: null, determinism: null, selftest: null });

  function check(title, desc, controls, runFn, render, key) {
    const result = h('div.check-result');
    const btn = h('button.btn.primary', { type: 'button' }, icon('play'), 'Run check');
    const panel = h('section.panel', h('div.panel-body',
      h('div.check-row', h('div', h('h2', title), h('p.muted', { style: { marginTop: '4px', fontSize: '13px', maxWidth: '72ch' } }, desc)),
        h('div', { style: { display: 'flex', gap: '10px', alignItems: 'flex-end', flexWrap: 'wrap', justifyContent: 'flex-end' } }, controls, btn)),
      result));
    btn.addEventListener('click', async () => {
      btn.classList.add('loading');
      btn.disabled = true;
      result.replaceChildren(h('div.skeleton', { style: { height: '64px' } }));
      try {
        S[key] = await runFn();
        result.replaceChildren(render(S[key]));
      } catch (e) {
        result.replaceChildren(DL.errorCallout(e));
      } finally {
        btn.classList.remove('loading');
        btn.disabled = false;
      }
    });
    if (S[key]) result.appendChild(render(S[key]));
    return panel;
  }

  DL.views.correctness = {
    mount(root) {
      const page = h('div.page.fade-in');
      root.appendChild(page);
      page.appendChild(h('div.page-head', h('div', h('h1', 'Correctness'),
        h('p', 'The same checks ./scripts/verify.sh runs on every build, run live against the engine. Verify runs the full 10,000-query agreement suite; here you choose the size.'))));

      if (!DL.api.live) {
        page.appendChild(DL.serverRequired('These checks run on the engine', 'Agreement, determinism and invariant checks execute the C++ code in real time. Start the server to run them from here.'));
        return () => {};
      }

      const info = DL.api.info;
      page.appendChild(h('div.panel', h('dl.rows',
        h('div.row', h('dt', 'Graph integrity'), h('dd', info.graph.validate === 'ok' ? h('span.badge.ok', icon('check'), 'CSR, reverse CSR and v_max bound valid') : h('span.badge.err', info.graph.validate))),
        h('div.row', h('dt', 'Connectivity'), h('dd', info.graph.strongly_connected ? h('span.badge.ok', icon('check'), 'Single strongly connected component') : h('span.badge.err', 'Not strongly connected'))),
        h('div.row', h('dt', 'Hash function'), h('dd', 'FNV-1a 64 over a canonical little-endian state serialization')))));

      // 1. Agreement
      const queries = DL.numberInput(300, 1, 5000);
      const traffic = h('select.input', ['free', 'moderate', 'heavy'].map(t => h('option', { value: t }, t[0].toUpperCase() + t.slice(1))));
      queries.style.width = '96px';
      page.appendChild(h('div.section', check('Algorithm agreement',
        'Random source–destination pairs from a fixed seed. Dijkstra, Bidirectional Dijkstra, A* and CCH must return the same integer cost, and every reconstructed path must sum to that cost.',
        [DL.field('Queries', queries), DL.field('Traffic', traffic)],
        () => {
          const q = DL.readInt(queries, 1, 5000);
          if (q === null) throw new Error('Queries must be between 1 and 5000.');
          return DL.api.get('/api/agreement', { queries: q, traffic: traffic.value }, 600000);
        },
        r => h('div.fade-in',
          h('div', { style: { display: 'flex', gap: '8px', flexWrap: 'wrap', alignItems: 'center', marginBottom: '12px' } },
            r.failures === 0 ? h('span.badge.ok', icon('check'), `${fmt.int(r.queries)} of ${fmt.int(r.queries)} queries agree`) : h('span.badge.err', icon('x'), `${r.failures} mismatches`),
            h('span.muted', { style: { fontSize: '13px' } }, `cost digest`), h('span.hash', r.cost_digest),
            h('span.muted', { style: { fontSize: '13px' } }, `· ${r.traffic} traffic · seed ${r.seed} · ${fmt.num(r.wall_ms / 1000, 1)} s`)),
          h('div.panel', { style: { boxShadow: 'none' } }, h('table.data.compact',
            h('thead', h('tr', h('th', 'Algorithm'), h('th.r', 'Median latency'), h('th.r', 'p95 latency'), h('th.r', 'Mean nodes settled'))),
            h('tbody', r.algorithms.map(a => h('tr', h('td', DL.algoTag(DL.ALGOS.find(x => x.id === a.algorithm))), h('td.r', fmt.us(a.median_us)), h('td.r', fmt.us(a.p95_us)), h('td.r', fmt.int(a.mean_settled))))))),
          r.mismatches.length ? h('pre.mono', { style: { marginTop: '12px', fontSize: '12px' } }, JSON.stringify(r.mismatches, null, 2)) : null),
        'agreement')));

      // 2. Determinism
      const seed = DL.numberInput(42, 0, 2147483647);
      seed.style.width = '110px';
      const policy = h('select.input', DL.POLICIES.map(p => h('option', { value: p.id }, p.label)));
      const hashCell = (label, value, ok) => h('div', h('div.label', label), h('span.hash', value), ok === undefined ? null : h('span', { class: ok ? 'ok-ic' : 'err-ic', style: { marginLeft: '6px' } }, icon(ok ? 'check' : 'x')));
      page.appendChild(h('div.section', check('Determinism and replay',
        'Runs one scenario twice and compares the event logs byte for byte and the final state hashes. Then it rebuilds the state from the serialized log alone, with no routing and no dispatch, and checks the hash matches. A different seed must give a different hash.',
        [DL.field('Seed', seed), DL.field('Policy', policy)],
        () => {
          const s = DL.readInt(seed, 0, 2147483647);
          if (s === null) throw new Error('Seed must be a non-negative integer.');
          return DL.api.get('/api/determinism', { seed: s, policy: policy.value });
        },
        r => h('div.fade-in',
          h('div', { style: { display: 'flex', gap: '8px', flexWrap: 'wrap', marginBottom: '12px' } },
            r.deterministic ? h('span.badge.ok', icon('check'), 'Deterministic') : h('span.badge.err', icon('x'), 'Not deterministic'),
            r.logs_identical ? h('span.badge', `Logs identical · ${fmt.int(r.events)} events · ${fmt.bytes(r.log_bytes)}`) : h('span.badge.err', 'Logs differ'),
            r.replay_routing_free ? h('span.badge', 'Replay made zero routing calls') : h('span.badge.err', 'Replay routed')),
          h('div.split-hash',
            hashCell('Run 1 final state', r.hash_run1),
            hashCell('Run 2 final state', r.hash_run2, r.hash_run2 === r.hash_run1),
            hashCell('Replayed from log', r.replay_hash, r.replay_hash === r.hash_run1),
            hashCell(`Seed ${r.other_seed} (control)`, r.hash_other_seed, r.hash_other_seed !== r.hash_run1)),
          h('p.faint', { style: { fontSize: '12px', marginTop: '10px' } }, `Scenario ${r.scenario.scenario_hash} · ${r.scenario.couriers} couriers · ${r.scenario.orders} orders · ${r.scenario.traffic} traffic · log digests ${r.log_digest_run1} / ${r.log_digest_run2}`)),
        'determinism')));

      // 3. Invariant self-test
      const KIND = {
        delivery_before_pickup: 'Delivery before pickup', double_delivery: 'Order delivered twice', time_backwards: 'Time moves backward',
        courier_double_assignment: 'Courier double-assigned', pickup_before_food_ready: 'Pickup before food is ready',
      };
      page.appendChild(h('div.section', check('Invariant enforcement',
        'Simulates a scenario, then feeds the replay deliberately corrupted copies of its log, one per PRD §13.3 invariant. Every corruption must be rejected by the same state machine that drives the simulator.',
        [],
        () => DL.api.get('/api/selftest'),
        r => h('div.fade-in',
          h('div', { style: { marginBottom: '8px' } }, r.detected === r.total ? h('span.badge.ok', icon('check'), `${r.detected} of ${r.total} corruptions rejected`) : h('span.badge.err', `${r.detected} of ${r.total} rejected`)),
          h('div', r.cases.map(c => h('div.case', h('span', { class: c.detected ? 'ok-ic' : 'err-ic' }, icon(c.detected ? 'check' : 'x')),
            h('span', KIND[c.kind] || c.kind), h('span.msg', c.message || 'not detected'))))),
        'selftest')));

      return () => {};
    },
  };
})();
