// Data source for the console. Live mode talks to `dispatchlab serve` on the
// same origin (relative /api/ paths only; never another host). Offline mode
// (opened from file://, or no server answering) uses the bundles written by
// `dispatchlab export-viewer`: window.DISPATCHLAB_REPLAY / DISPATCHLAB_RESULTS.
(function () {
  'use strict';
  const DL = (window.DL = window.DL || {});

  class ApiError extends Error {}

  const api = {
    mode: 'offline',
    info: null,

    // Detects whether the engine is reachable. Resolves to 'live' or 'offline'.
    async connect() {
      if (!location.protocol.startsWith('http')) return (this.mode = 'offline');
      try {
        this.info = await this.get('/api/info', null, 4000);
        this.mode = 'live';
      } catch (e) {
        this.mode = 'offline';
      }
      return this.mode;
    },

    get live() { return this.mode === 'live'; },

    async get(path, params, timeoutMs = 120000) {
      if (!path.startsWith('/api/')) throw new ApiError('invalid API path');
      const qs = params ? '?' + new URLSearchParams(Object.entries(params).filter(([, v]) => v !== undefined && v !== null)).toString() : '';
      const ctl = new AbortController();
      const timer = setTimeout(() => ctl.abort(), timeoutMs);
      let res;
      try {
        res = await fetch(path + qs, { signal: ctl.signal, cache: 'no-store' });
      } catch (e) {
        throw new ApiError(e.name === 'AbortError' ? 'The engine took too long to answer.' : 'Cannot reach the engine. Is `dispatchlab serve` still running?');
      } finally {
        clearTimeout(timer);
      }
      let body = null;
      try { body = await res.json(); } catch (e) { /* non-JSON */ }
      if (!res.ok) throw new ApiError((body && body.error) || `Request failed (${res.status})`);
      return body;
    },

    // Road network as typed arrays plus a nearest-node index (cached).
    async network() {
      if (this._net) return this._net;
      let raw;
      if (this.live) raw = await this.get('/api/network', null, 60000);
      else if (window.DISPATCHLAB_REPLAY) raw = window.DISPATCHLAB_REPLAY;
      else return null;
      this._net = DL.buildNetwork(raw);
      return this._net;
    },

    async results() {
      if (this._results !== undefined) return this._results;
      if (this.live) this._results = await this.get('/api/results');
      else this._results = window.DISPATCHLAB_RESULTS || null;
      return this._results;
    },

    // The recorded replay that ships with the console (offline mode).
    recordedReplay() { return window.DISPATCHLAB_REPLAY || null; },
  };

  // Network container shared by every map.
  DL.buildNetwork = function (raw) {
    const n = raw.nodes.length / 2;
    const lon = new Float64Array(n), lat = new Float64Array(n);
    for (let i = 0; i < n; i++) { lon[i] = raw.nodes[2 * i]; lat[i] = raw.nodes[2 * i + 1]; }
    const edges = Int32Array.from(raw.edges);
    const arterial = Uint8Array.from(raw.edge_arterial);
    const bbox = raw.bbox;
    // Uniform grid index for nearest-node lookups on click.
    const G = 128;
    const cellW = (bbox[2] - bbox[0]) / G || 1, cellH = (bbox[3] - bbox[1]) / G || 1;
    const buckets = Array.from({ length: G * G }, () => []);
    const cellOf = (x, y) => [Math.min(G - 1, Math.max(0, Math.floor((x - bbox[0]) / cellW))), Math.min(G - 1, Math.max(0, Math.floor((y - bbox[1]) / cellH)))];
    for (let i = 0; i < n; i++) { const [cx, cy] = cellOf(lon[i], lat[i]); buckets[cy * G + cx].push(i); }
    const k = Math.cos(((bbox[1] + bbox[3]) / 2) * Math.PI / 180);
    function nearest(x, y) {
      const [cx, cy] = cellOf(x, y);
      let best = -1, bestD = Infinity;
      for (let r = 0; r < G; r++) {
        for (let gy = cy - r; gy <= cy + r; gy++) {
          for (let gx = cx - r; gx <= cx + r; gx++) {
            if (gx < 0 || gy < 0 || gx >= G || gy >= G) continue;
            if (Math.max(Math.abs(gx - cx), Math.abs(gy - cy)) !== r) continue;
            for (const i of buckets[gy * G + gx]) {
              const dx = (lon[i] - x) * k, dy = lat[i] - y, d = dx * dx + dy * dy;
              if (d < bestD) { bestD = d; best = i; }
            }
          }
        }
        if (best >= 0 && r >= 1) break;
      }
      return best;
    }
    return { n, lon, lat, edges, arterial, bbox, nearest, raw };
  };

  DL.api = api;
  DL.ApiError = ApiError;
})();
