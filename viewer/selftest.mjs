// Headless self-test of the viewer logic (viewer/core.js) against a replay
// document: node viewer/selftest.mjs viewer/replay.json
// Checks that the viewer reconstructs the same outcome as the simulator.
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const sandbox = {};
sandbox.globalThis = sandbox;
runInNewContext(readFileSync(join(here, 'core.js'), 'utf8'), sandbox);
const C = sandbox.DLCore;

const path = process.argv[2] || join(here, 'replay.json');
const doc = JSON.parse(readFileSync(path, 'utf8'));
const model = C.buildModel(doc);
const fail = msg => { console.error('VIEWER_SELFTEST_FAIL: ' + msg); process.exit(1); };

const counts = {};
for (const e of model.events) counts[e.event] = (counts[e.event] || 0) + 1;
if (Object.keys(counts).length !== 7) fail('expected 7 event types, got ' + Object.keys(counts).length);

// Start: nothing created, every courier idle at its initial node.
const s0 = C.stateAt(model, -1);
if (s0.metrics.created !== 0) fail('orders exist before t=0');
s0.couriers.forEach((c, i) => {
  if (c.lon !== model.lon[model.couriers[i].node] || c.lat !== model.lat[model.couriers[i].node]) fail('initial courier position');
});

// End: viewer's delivered count equals the log's.
const end = C.stateAt(model, model.endTime);
if (end.metrics.delivered !== (counts.ORDER_DELIVERED || 0)) fail('delivered count mismatch');
if (end.metrics.created !== (counts.ORDER_CREATED || 0)) fail('created count mismatch');
if (end.eventCount !== model.events.length) fail('not all events applied at end');

// Sweep the timeline: monotone counters, positions finite and inside the bbox.
const [minLon, minLat, maxLon, maxLat] = model.bbox;
let prevDelivered = 0, samples = 0, moving = 0;
for (let t = 0; t <= model.endTime; t += Math.max(1, Math.floor(model.endTime / 400))) {
  const s = C.stateAt(model, t);
  if (s.metrics.delivered < prevDelivered) fail('delivered count decreased');
  prevDelivered = s.metrics.delivered;
  if (s.metrics.available + s.couriers.filter(c => c.status !== 'idle').length !== model.couriers.length) fail('courier status accounting');
  for (const c of s.couriers) {
    if (!Number.isFinite(c.lon) || !Number.isFinite(c.lat)) fail('non-finite courier position');
    if (c.lon < minLon - 1e-6 || c.lon > maxLon + 1e-6 || c.lat < minLat - 1e-6 || c.lat > maxLat + 1e-6) fail('courier outside bbox');
    if (c.leg) moving++;
  }
  samples++;
}
if (moving === 0) fail('no courier movement observed');

// Interpolation: a leg starts at its first path node and ends at its last.
for (const leg of model.legs.slice(0, 50)) {
  const a = C.legPosition(model, leg, leg.t0), b = C.legPosition(model, leg, leg.t1);
  const p0 = leg.path[0], p1 = leg.path[leg.path.length - 1];
  if (Math.abs(a[0] - model.lon[p0]) > 1e-9 || Math.abs(a[1] - model.lat[p0]) > 1e-9) fail('leg start position');
  if (Math.abs(b[0] - model.lon[p1]) > 1e-9 || Math.abs(b[1] - model.lat[p1]) > 1e-9) fail('leg end position');
}

console.log(`viewer selftest: events=${model.events.length} legs=${model.legs.length} samples=${samples} ` +
  `delivered=${end.metrics.delivered}/${model.orders.length} avg=${end.metrics.avgDeliveryMin.toFixed(2)} min`);
console.log('VIEWER_SELFTEST_OK');
