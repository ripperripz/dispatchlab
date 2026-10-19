// Routing lab: pick two points, run all four algorithms on the engine, and
// watch their search spaces unfold in settle order, side by side.
(function () {
  'use strict';
  const DL = window.DL;
  const { h, icon, fmt } = DL;
  DL.views = DL.views || {};
  DL.state = DL.state || {};
  const S = (DL.state.routing = DL.state.routing || { s: null, t: null, traffic: 'free', result: null, selected: 'dijkstra' });

  const SWEEP_MS = 1500;
  const coarse = () => matchMedia('(pointer: coarse)').matches;
  const ease = x => 1 - Math.pow(1 - x, 3);

  DL.views.routing = {
    mount(root) {
      root.className = 'view fill';
      const ws = h('div.workspace.fade-in');
      root.appendChild(ws);
      const stage = h('div.stage');
      const host = h('div.map-host');
      const strip = h('div.strip');
      stage.append(host, strip);
      const insp = h('aside.inspector', { 'aria-label': 'Route inspector' });
      ws.append(stage, insp);

      let alive = true, map = null, minis = [], net = null, raf = 0, armed = 'a', inflight = 0;
      let sweepStart = 0, progress = 1;

      const canvas = h('canvas.map-canvas.pick', { 'aria-label': 'Map: click to place origin and destination' });
      host.appendChild(canvas);
      const hint = h('div.map-hint', icon('route'), h('span', 'Click the map to place the origin'));
      host.appendChild(hint);

      // ---- inspector
      const endA = h('button.endpoint', { type: 'button', style: { width: '100%', textAlign: 'left', cursor: 'pointer' } });
      const endB = h('button.endpoint', { type: 'button', style: { width: '100%', textAlign: 'left', cursor: 'pointer' } });
      endA.addEventListener('click', () => arm('a'));
      endB.addEventListener('click', () => arm('b'));
      const trafficCtl = DL.segmented(
        [{ value: 'free', label: 'Free flow' }, { value: 'moderate', label: 'Moderate' }, { value: 'heavy', label: 'Heavy' }],
        S.traffic, v => { S.traffic = v; if (S.s !== null && S.t !== null) run(); }, { full: true, label: 'Traffic scenario' });
      const randomBtn = h('button.btn', { type: 'button', onclick: randomPair, title: 'Random origin and destination (R)' }, icon('shuffle'), 'Random pair');
      const swapBtn = h('button.btn.icon', { type: 'button', onclick: swap, 'aria-label': 'Swap origin and destination' }, icon('swap'));
      const resultBox = h('div');
      const replayBtn = h('button.btn.sm', { type: 'button', onclick: () => sweep(), disabled: true }, icon('restart'), 'Replay search');

      insp.append(
        h('section.insp-section',
          h('h2', 'Route', h('span.kbd', 'R')),
          endA, endB,
          h('div', { style: { display: 'flex', gap: '8px', marginTop: '10px' } }, randomBtn, swapBtn)),
        h('section.insp-section', h('h2', 'Traffic'), trafficCtl,
          h('p.faint', { style: { fontSize: '12px', marginTop: '8px' } }, 'Synthetic multipliers ≥ 1 on edge weights (docs/DATA.md). A* stays admissible under all three.')),
        h('section.insp-section.grow', h('h2', 'Result', replayBtn), resultBox));

      if (!DL.api.live) {
        hint.hidden = true;
        insp.replaceChildren(h('section.insp-section', DL.serverRequired('The routing lab runs on the engine',
          'Every query runs Dijkstra, Bidirectional Dijkstra, A* and CCH in C++ on the 142k-node graph, and returns their costs, latencies and search spaces.')));
      }

      function endpointView(btn, which, node) {
        const label = which === 'a' ? 'Origin' : 'Destination';
        btn.replaceChildren(
          h('span.pin.' + which, which.toUpperCase()),
          h('div.meta',
            h('div', node === null ? h('span.muted', `${label}: click the map`) : h('span', label, h('span.faint', ` · node ${fmt.int(node)}`))),
            node === null ? null : h('div.faint.mono', `${net.lat[node].toFixed(5)}, ${net.lon[node].toFixed(5)}`)));
        btn.classList.toggle('armed', armed === which);
        btn.setAttribute('aria-pressed', String(armed === which));
      }
      function refreshEndpoints() {
        if (!net) return;
        endpointView(endA, 'a', S.s);
        endpointView(endB, 'b', S.t);
        const verb = coarse() ? 'Tap' : 'Click';
        hint.lastChild.textContent = armed === 'a' ? `${verb} the map to place the origin` : armed === 'b' ? `${verb} the map to place the destination`
          : coarse() ? 'Drag to pan · pinch to zoom · tap an endpoint to move it' : 'Drag to pan · scroll to zoom · click an endpoint to move it';
      }
      function arm(which) { armed = which; canvas.classList.toggle('pick', !!armed); refreshEndpoints(); }

      function randomPair() {
        if (!net || !DL.api.live) return;
        S.s = Math.floor(Math.random() * net.n);
        S.t = Math.floor(Math.random() * net.n);
        armed = null;
        refreshEndpoints();
        run();
      }
      function swap() {
        if (S.s === null || S.t === null) return;
        [S.s, S.t] = [S.t, S.s];
        refreshEndpoints();
        run();
      }

      // ---- running a query
      async function run() {
        if (!DL.api.live || S.s === null || S.t === null) return;
        const id = ++inflight;
        resultBox.replaceChildren(h('div', { style: { display: 'grid', gap: '10px' } },
          h('div.skeleton', { style: { height: '34px', width: '60%' } }),
          h('div.skeleton', { style: { height: '150px' } })));
        hint.lastChild.textContent = 'Running four algorithms on the engine…';
        try {
          const res = await DL.api.get('/api/route', { s: S.s, t: S.t, traffic: S.traffic, trace: 1 });
          if (!alive || id !== inflight) return;
          S.result = res;
          showResult();
          frameMinis();
          sweep();
        } catch (e) {
          if (!alive || id !== inflight) return;
          resultBox.replaceChildren(DL.errorCallout(e));
        } finally {
          if (alive && id === inflight) refreshEndpoints();
        }
      }

      function showResult() {
        const r = S.result;
        replayBtn.disabled = !r;
        if (!r) {
          resultBox.replaceChildren(h('p.muted', { style: { fontSize: '13px' } }, 'Place an origin and a destination, or press R for a random pair. All four algorithms run on every query.'));
          return;
        }
        if (!r.reachable) {
          resultBox.replaceChildren(h('div.callout', icon('alert'), h('div', 'No route: the destination is unreachable from the origin.')));
          return;
        }
        const maxSettled = Math.max(...r.results.map(x => x.settled));
        const rows = r.results.map(x => {
          const meta = DL.ALGOS.find(a => a.id === x.algorithm);
          const tr = h('tr.clickable', { 'aria-selected': String(S.selected === x.algorithm), tabindex: 0 },
            h('td', DL.algoTag(meta)),
            h('td', h('div.bar-cell', { style: { minWidth: '96px' } },
              h('div.bar-track', h('div.bar-fill', { style: { width: `${Math.max(1.5, (x.settled / maxSettled) * 100)}%`, background: meta.color } })),
              h('span.num', { style: { minWidth: '52px', textAlign: 'right', fontSize: '12px' } }, fmt.int(x.settled)))),
            h('td.r', fmt.us(x.latency_us)));
          const pick = () => { S.selected = x.algorithm; showResult(); syncMinis(); map.render(); };
          tr.addEventListener('click', pick);
          tr.addEventListener('keydown', ev => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); pick(); } });
          return tr;
        });
        const fastest = r.results.reduce((a, b) => (b.latency_us < a.latency_us ? b : a));
        const dij = r.results[0];
        resultBox.replaceChildren(
          h('div.summary-line', h('span.big', fmt.dur(r.cost_ms)), h('span.muted', `${fmt.km(r.length_m)} · ${fmt.int(r.path.length - 1)} segments`)),
          h('div', { style: { display: 'flex', gap: '6px', flexWrap: 'wrap', margin: '10px 0 14px' } },
            r.agree ? h('span.badge.ok', icon('check'), '4 of 4 agree on cost') : h('span.badge.err', icon('x'), 'Algorithms disagree'),
            h('span.badge', `${S.traffic === 'free' ? 'Free flow' : S.traffic} traffic`)),
          h('div.panel', { style: { boxShadow: 'none' } }, h('table.data.compact',
            h('thead', h('tr', h('th', 'Algorithm'), h('th', 'Nodes settled'), h('th.r', 'Latency'))),
            h('tbody', rows))),
          h('p.muted', { style: { fontSize: '12px', marginTop: '12px' } },
            `${DL.ALGOS.find(a => a.id === fastest.algorithm).label} answered in ${fmt.us(fastest.latency_us)}, settling ${fmt.int(fastest.settled)} nodes where Dijkstra settled ${fmt.int(dij.settled)}. Latency is the median of three engine runs with path reconstruction.`));
      }

      // ---- drawing
      function trace(algo) {
        const r = S.result;
        if (!r) return null;
        return r.results.find(x => x.algorithm === algo);
      }
      function drawSearch(ctx, p, dpr, algo, k, emphasizeFrontier) {
        const tr = trace(algo);
        if (!tr) return;
        const meta = DL.ALGOS.find(a => a.id === algo);
        const color = DL.cssVar(`--s-${algo}`);
        const n = Math.floor(tr.trace.length * k);
        const sz = Math.max(1.6, 1.4 * dpr);
        ctx.fillStyle = color;
        ctx.globalAlpha = algo === 'cch' ? 0.95 : 0.32;
        const frontier = emphasizeFrontier ? Math.max(0, n - Math.ceil(tr.trace.length * 0.035)) : n;
        for (let i = 0; i < frontier; i++) {
          const v = tr.trace[i];
          ctx.fillRect(p.x(net.lon[v]) - sz / 2, p.y(net.lat[v]) - sz / 2, sz, sz);
        }
        if (frontier < n) {
          ctx.globalAlpha = 1;
          const big = sz * 1.5;
          for (let i = frontier; i < n; i++) {
            const v = tr.trace[i];
            ctx.fillRect(p.x(net.lon[v]) - big / 2, p.y(net.lat[v]) - big / 2, big, big);
          }
        }
        ctx.globalAlpha = 1;
        void meta;
      }
      function drawPath(ctx, p, dpr, width) {
        const r = S.result;
        if (!r || !r.path.length) return;
        ctx.lineJoin = 'round';
        ctx.lineCap = 'round';
        const trace = () => {
          ctx.beginPath();
          r.path.forEach((v, i) => (i ? ctx.lineTo(p.x(net.lon[v]), p.y(net.lat[v])) : ctx.moveTo(p.x(net.lon[v]), p.y(net.lat[v]))));
        };
        trace();
        ctx.strokeStyle = DL.cssVar('--map-casing');
        ctx.lineWidth = (width + 3) * dpr;
        ctx.stroke();
        trace();
        ctx.strokeStyle = DL.cssVar('--accent');
        ctx.lineWidth = width * dpr;
        ctx.stroke();
      }
      function drawPin(ctx, p, dpr, node, label, fill, text) {
        if (node === null) return;
        const x = p.x(net.lon[node]), y = p.y(net.lat[node]), r = 9 * dpr;
        ctx.beginPath();
        ctx.arc(x, y, r + 2.5 * dpr, 0, Math.PI * 2);
        ctx.fillStyle = DL.cssVar('--map-casing');
        ctx.fill();
        ctx.beginPath();
        ctx.arc(x, y, r, 0, Math.PI * 2);
        ctx.fillStyle = fill;
        ctx.fill();
        ctx.fillStyle = text;
        ctx.font = `600 ${10.5 * dpr}px Geist, system-ui, sans-serif`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(label, x, y + 0.5 * dpr);
      }
      function mainOverlay(ctx, p, dpr) {
        if (S.result) {
          drawSearch(ctx, p, dpr, S.selected, ease(progress), progress < 1);
          if (progress >= 1 || S.selected === 'cch') drawPath(ctx, p, dpr, 3);
        }
        drawPin(ctx, p, dpr, S.s, 'A', DL.cssVar('--text'), DL.cssVar('--bg'));
        drawPin(ctx, p, dpr, S.t, 'B', DL.cssVar('--accent'), '#ffffff');
      }

      function frameMinis() {
        const dij = trace('dijkstra');
        if (!dij) return;
        const bounds = DL.MapView.boundsOf(net, dij.trace.concat(S.result.path), 0.06);
        minis.forEach(m => m.map.fit(bounds));
        map.fit(DL.MapView.boundsOf(net, dij.trace.concat(S.result.path), 0.12));
      }
      function syncMinis() {
        minis.forEach(m => {
          m.el.setAttribute('aria-pressed', String(m.algo.id === S.selected));
          // Final counts (same numbers as the results table); only the drawing animates.
          const tr = trace(m.algo.id);
          m.stat.textContent = tr ? `${fmt.compact(tr.settled)} · ${fmt.us(tr.latency_us)}` : '–';
          m.map.render();
        });
      }

      function sweep() {
        if (!S.result) return;
        cancelAnimationFrame(raf);
        sweepStart = performance.now();
        progress = 0;
        const tick = now => {
          if (!alive) return;
          progress = Math.min(1, (now - sweepStart) / SWEEP_MS);
          map.renderNow();
          syncMinis();
          if (progress < 1) raf = requestAnimationFrame(tick);
        };
        raf = requestAnimationFrame(tick);
      }

      function onKey(ev) {
        const tag = (ev.target.tagName || '').toLowerCase();
        if (tag === 'input' || tag === 'select' || ev.metaKey || ev.ctrlKey) return;
        if (ev.key === 'r' || ev.key === 'R') randomPair();
        if (ev.key === 'Escape') arm(null);
      }
      document.addEventListener('keydown', onKey);

      (async () => {
        net = await DL.api.network();
        if (!alive) return;
        if (!net) {
          host.replaceChildren(h('div.panel-body', DL.serverRequired('No network available', 'Start the engine to load the road network.')));
          return;
        }
        // Keep fitted routes clear of the hint (top) and legend (bottom) overlays.
        map = new DL.MapView(canvas, net, { insetTop: 52, insetBottom: 48 });
        host.appendChild(DL.MapView.tools(map));
        map.addOverlay(mainOverlay);
        map.onPick = (lon, lat) => {
          if (!DL.api.live) return;
          const node = net.nearest(lon, lat);
          if (node < 0) return;
          if (armed === 'a' || armed === null && S.s === null) { S.s = node; armed = S.t === null ? 'b' : null; }
          else if (armed === 'b') { S.t = node; armed = null; }
          else return;
          canvas.classList.toggle('pick', !!armed);
          refreshEndpoints();
          if (S.s !== null && S.t !== null) run();
        };
        host.appendChild(h('div.map-legend',
          h('span', h('span.legend-dot', { style: { background: 'var(--s-dijkstra)', opacity: 0.6 } }), 'Settled nodes (selected algorithm)'),
          h('span', h('span.legend-line', { style: { background: 'var(--accent)', height: '3px' } }), 'Shortest path')));

        for (const algo of DL.ALGOS) {
          const c = h('canvas', { 'aria-hidden': 'true' });
          const stat = h('span.stat', '–');
          const el = h('div.mini', { role: 'button', tabindex: 0, 'aria-label': `Show ${algo.label} search space`, 'aria-pressed': String(S.selected === algo.id) },
            c, h('div.mini-label', h('span.algo', h('span.swatch', { style: { background: algo.color } }), algo.label), stat));
          strip.appendChild(el);
          const mm = new DL.MapView(c, net, { interactive: false, padding: 8, roadScale: 0.8 });
          mm.addOverlay((ctx, p, dpr) => {
            drawSearch(ctx, p, dpr, algo.id, ease(progress), false);
            drawPath(ctx, p, dpr, 1.6);
          });
          const choose = () => { S.selected = algo.id; showResult(); syncMinis(); map.render(); };
          el.addEventListener('click', choose);
          el.addEventListener('keydown', ev => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); choose(); } });
          minis.push({ algo, map: mm, el, stat });
        }

        if (S.s === null && S.t === null) armed = 'a'; else armed = null;
        canvas.classList.toggle('pick', !!armed);
        refreshEndpoints();
        showResult();
        if (S.result) { frameMinis(); progress = 1; syncMinis(); }
        else if (DL.api.live) randomPair();  // first visit: show a real query immediately
      })().catch(e => { if (alive) resultBox.replaceChildren(DL.errorCallout(e)); });

      return () => {
        alive = false;
        cancelAnimationFrame(raf);
        document.removeEventListener('keydown', onKey);
        if (map) map.destroy();
        minis.forEach(m => m.map.destroy());
      };
    },
  };
})();
