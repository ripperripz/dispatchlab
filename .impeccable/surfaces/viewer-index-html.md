---
version: 1
slug: "viewer-index-html"
primary_target: "viewer/index.html"
related_targets: ["viewer/app.js","viewer/style.css"]
---

# Surface brief: DispatchLab console (viewer/)

Scope: the whole local frontend (`viewer/index.html` and its scripts), served live by `dispatchlab serve`
and also opened offline from `file://`. Mode: Operate.

Audience/job: course evaluators, interviewers and the developer interrogate the engine: replay
simulations, run and compare routes live, compare dispatch policies, read benchmarks, prove correctness.
Constraints: no external hosts at runtime (fonts self-hosted), offline replay must keep working
(verify AC-13), every number comes from the engine or its result files.

## Direction contract

THESIS: A developer-platform console for a routing engine, in the register of Vercel, Cloudflare and
Linear (user-pinned). It refuses the logistics "control room" (dark map, neon routes, gauge cards):
the map is a precise instrument inside a calm product shell.

OWN-WORLD: Geist Sans and Geist Mono (self-hosted). Neutral greys with hairline borders and quiet 8px
radii, in light and dark themes. Inverted solid primary buttons. One orange brand/route accent. Four
algorithm series colours (Dijkstra blue, Bidirectional teal, A* violet, CCH pink), used consistently
everywhere.

STORY: The visitor sees the real Tricity network and the engine's live status, runs a route, and
watches four algorithms answer identically while exploring very different amounts of the city. They
then simulate deliveries and compare policies with measured, honest numbers.

FIRST VIEWPORT: 232px left sidebar (wordmark, six nav items, connection status, theme). Content
header with title, one-line description and right-aligned actions. On the Routing lab, the map fills
the work area and a 360px inspector on the right holds origin/destination, traffic and the results
table. A strip of four small-multiple search-space maps runs along the bottom.

FORM: user-pinned canon (Vercel × Cloudflare × Linear), overriding the roll; the roll's assigned
candidate was not used. Signature interaction: search spaces as animated small multiples, revealing
settled nodes in settle order. Motion grammar: 150–200 ms ease-out on state changes; the one authored
moment is the search sweep. seed: 8080f7ce2c244cea

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
