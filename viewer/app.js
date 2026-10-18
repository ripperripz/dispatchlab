// DispatchLab viewer UI: canvas rendering, playback controls and panels.
// Data comes from window.DISPATCHLAB_REPLAY (replay_data.js, works from
// file:// without a server) or from a replay.json chosen/dropped by the user.
(function () {
  'use strict';
  const C = window.DLCore;
  const $ = id => document.getElementById(id);
  const canvas = $('map');
  const ctx = canvas.getContext('2d');

  let model = null;
  let state = null;
  let t = 0;
  let playing = false;
  let lastFrame = 0;
  let roadLayer = null;      // offscreen canvas with the road network
  let view = { zoom: 1, panX: 0, panY: 0 };
  let proj = null;
  let lastTimelineK = -1;

  const css = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  function load(doc) {
    try {
      model = C.buildModel(doc);
    } catch (err) {
      alert('Could not load replay: ' + err.message);
      return;
    }
    const m = model.meta;
    $('meta').textContent = `policy ${m.policy} · routing ${m.routing} · traffic ${m.traffic} · seed ${m.seed} · ` +
      `${m.couriers} couriers · ${m.orders} orders · ${model.events.length} events · final hash ${m.final_state_hash}`;
    $('drop').classList.add('hidden');
    $('scrub').max = String(model.endTime);
    $('ev-count').textContent = `(${model.events.length})`;
    view = { zoom: 1, panX: 0, panY: 0 };
    t = 0;
    playing = false;
    $('play').textContent = 'Play';
    resize();
  }

  function resize() {
    const dpr = window.devicePixelRatio || 1;
    const r = canvas.getBoundingClientRect();
    canvas.width = Math.max(1, Math.round(r.width * dpr));
    canvas.height = Math.max(1, Math.round(r.height * dpr));
    roadLayer = null;
    render(true);
  }

  function currentProjection() {
    const dpr = window.devicePixelRatio || 1;
    const base = C.makeProjection(model.bbox, canvas.width, canvas.height, 16 * dpr);
    const cx = canvas.width / 2, cy = canvas.height / 2;
    return {
      x: lon => (base.x(lon) - cx) * view.zoom + cx + view.panX,
      y: lat => (base.y(lat) - cy) * view.zoom + cy + view.panY,
    };
  }

  function drawRoads() {
    roadLayer = document.createElement('canvas');
    roadLayer.width = canvas.width;
    roadLayer.height = canvas.height;
    const g = roadLayer.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    const e = model.edges, art = model.edgeArterial;
    for (const pass of [0, 1]) {  // local roads first, arterials on top
      g.beginPath();
      g.strokeStyle = pass ? css('--arterial') : css('--road');
      g.lineWidth = (pass ? 1.4 : 0.6) * dpr * Math.min(2, Math.sqrt(view.zoom));
      for (let i = 0; i < art.length; i++) {
        if (art[i] !== pass) continue;
        const a = e[2 * i], b = e[2 * i + 1];
        g.moveTo(proj.x(model.lon[a]), proj.y(model.lat[a]));
        g.lineTo(proj.x(model.lon[b]), proj.y(model.lat[b]));
      }
      g.stroke();
    }
  }

  function render(force) {
    if (!model) return;
    proj = currentProjection();
    if (!roadLayer) drawRoads();
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(roadLayer, 0, 0);
    state = C.stateAt(model, t);
    const dpr = window.devicePixelRatio || 1;

    // Active routes.
    ctx.lineWidth = 2 * dpr;
    for (const c of state.couriers) {
      if (!c.leg) continue;
      ctx.strokeStyle = css(c.status === C.STATUS.TO_CUSTOMER ? '--to_customer' : '--to_restaurant');
      ctx.globalAlpha = 0.55;
      ctx.beginPath();
      const p = c.leg.path;
      ctx.moveTo(proj.x(c.lon), proj.y(c.lat));
      const f = (t - c.leg.t0) / Math.max(1, c.leg.t1 - c.leg.t0);
      for (let i = 0; i < p.length; i++) {
        if (c.leg.cum[i] < f) continue;
        ctx.lineTo(proj.x(model.lon[p[i]]), proj.y(model.lat[p[i]]));
      }
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    // Restaurants.
    ctx.fillStyle = css('--restaurant');
    const rs = 4 * dpr;
    for (const r of model.restaurants) {
      ctx.fillRect(proj.x(model.lon[r.node]) - rs, proj.y(model.lat[r.node]) - rs, 2 * rs, 2 * rs);
    }
    // Customers of orders that exist and are not yet delivered.
    ctx.strokeStyle = css('--customer');
    ctx.lineWidth = 2 * dpr;
    for (const o of state.orders) {
      if (o.status === C.ORDER.NOT_CREATED || o.status === C.ORDER.DELIVERED) continue;
      const node = model.customers[model.orders[o.id].customer].node;
      ctx.beginPath();
      ctx.arc(proj.x(model.lon[node]), proj.y(model.lat[node]), 4.5 * dpr, 0, 2 * Math.PI);
      ctx.stroke();
    }
    // Couriers.
    for (const c of state.couriers) {
      ctx.fillStyle = css('--' + c.status);
      ctx.beginPath();
      ctx.arc(proj.x(c.lon), proj.y(c.lat), 4 * dpr, 0, 2 * Math.PI);
      ctx.fill();
    }
    updatePanels(force);
  }

  function updatePanels(force) {
    const m = state.metrics;
    $('m-time').textContent = C.formatTime(t);
    $('clock').textContent = C.formatTime(t);
    $('m-total').textContent = m.totalOrders;
    $('m-created').textContent = m.created;
    $('m-delivered').textContent = m.delivered;
    $('m-avg').textContent = m.delivered ? m.avgDeliveryMin.toFixed(1) + ' min' : '–';
    $('m-pending').textContent = m.pending;
    $('m-active').textContent = m.active;
    $('m-available').textContent = `${m.available} / ${model.couriers.length}`;
    $('scrub').value = String(Math.round(t));
    if (force || state.eventCount !== lastTimelineK) renderTimeline(state.eventCount);
  }

  // Shows the most recent events up to the current time (newest last).
  function renderTimeline(k) {
    lastTimelineK = k;
    const ol = $('timeline');
    const from = Math.max(0, k - 150);
    const frag = document.createDocumentFragment();
    for (let i = from; i < Math.min(model.events.length, k + 20); i++) {
      const e = model.events[i];
      const li = document.createElement('li');
      li.textContent = C.describeEvent(e);
      li.dataset.t = e.timestamp;
      if (i >= k) li.classList.add('muted');
      if (i === k - 1) li.classList.add('latest');
      frag.appendChild(li);
    }
    ol.replaceChildren(frag);
    const latest = ol.querySelector('.latest');
    if (latest) latest.scrollIntoView({ block: 'nearest' });
  }

  function tick(now) {
    if (playing && model) {
      const dt = lastFrame ? now - lastFrame : 0;
      t = Math.min(model.endTime, t + dt * Number($('speed').value));
      if (t >= model.endTime) { playing = false; $('play').textContent = 'Play'; }
      render(false);
    }
    lastFrame = now;
    requestAnimationFrame(tick);
  }

  // ---- controls
  $('play').addEventListener('click', () => {
    if (!model) return;
    if (t >= model.endTime) t = 0;
    playing = !playing;
    $('play').textContent = playing ? 'Pause' : 'Play';
  });
  $('replay').addEventListener('click', () => {
    if (!model) return;
    t = 0;
    playing = true;
    $('play').textContent = 'Pause';
    render(true);
  });
  $('scrub').addEventListener('input', ev => {
    if (!model) return;
    t = Number(ev.target.value);
    render(false);
  });
  $('timeline').addEventListener('click', ev => {
    const li = ev.target.closest('li');
    if (!li || !model) return;
    t = Number(li.dataset.t);
    render(true);
  });

  // Zoom (wheel) and pan (drag).
  canvas.addEventListener('wheel', ev => {
    if (!model) return;
    ev.preventDefault();
    const dpr = window.devicePixelRatio || 1;
    const factor = Math.exp(-ev.deltaY * 0.0015);
    const nz = Math.min(40, Math.max(0.5, view.zoom * factor));
    const r = canvas.getBoundingClientRect();
    const mx = (ev.clientX - r.left) * dpr - canvas.width / 2;
    const my = (ev.clientY - r.top) * dpr - canvas.height / 2;
    view.panX = mx - (mx - view.panX) * (nz / view.zoom);
    view.panY = my - (my - view.panY) * (nz / view.zoom);
    view.zoom = nz;
    roadLayer = null;
    render(false);
  }, { passive: false });
  let drag = null;
  canvas.addEventListener('pointerdown', ev => {
    drag = { x: ev.clientX, y: ev.clientY, px: view.panX, py: view.panY };
    canvas.classList.add('dragging');
    canvas.setPointerCapture(ev.pointerId);
  });
  canvas.addEventListener('pointermove', ev => {
    if (!drag || !model) return;
    const dpr = window.devicePixelRatio || 1;
    view.panX = drag.px + (ev.clientX - drag.x) * dpr;
    view.panY = drag.py + (ev.clientY - drag.y) * dpr;
    roadLayer = null;
    render(false);
  });
  canvas.addEventListener('pointerup', () => { drag = null; canvas.classList.remove('dragging'); });

  // Loading a replay.json from disk (works from file:// via FileReader).
  function readFile(file) {
    const reader = new FileReader();
    reader.onload = () => {
      try { load(JSON.parse(reader.result)); } catch (err) { alert('Invalid JSON: ' + err.message); }
    };
    reader.readAsText(file);
  }
  $('file').addEventListener('change', ev => { if (ev.target.files[0]) readFile(ev.target.files[0]); });
  const drop = $('drop');
  document.addEventListener('dragover', ev => { ev.preventDefault(); drop.classList.add('over'); });
  document.addEventListener('dragleave', () => drop.classList.remove('over'));
  document.addEventListener('drop', ev => {
    ev.preventDefault();
    drop.classList.remove('over');
    if (ev.dataTransfer.files[0]) readFile(ev.dataTransfer.files[0]);
  });
  window.addEventListener('resize', resize);
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => { roadLayer = null; render(true); });

  if (window.DISPATCHLAB_REPLAY) load(window.DISPATCHLAB_REPLAY);
  else drop.classList.remove('hidden');
  requestAnimationFrame(tick);
})();
