#!/usr/bin/env bash
# Builds a static, hostable demo of the console in dist/demo/: the viewer plus
# engine responses recorded by `dispatchlab export-demo`. No server is needed
# to open it; every result shown was computed by the C++ engine at build time.
#
#   ./scripts/build-demo.sh [graph]      # default data/processed/tricity.graph
#
# Outputs:
#   dist/demo/index.html    page body for an HTML host that supplies <html>/<head>
#   dist/demo/preview.html  the same page as a full document (open locally)
#   site/                   committed static site for Vercel or any static host
#                           (full index.html + vercel.json; no build step needed)
set -euo pipefail
cd "$(dirname "$0")/.."
GRAPH=${1:-data/processed/tricity.graph}
DL=${DL:-build/dispatchlab}
OUT=dist/demo

[ -x "$DL" ] || { echo "build the engine first: cmake --build build -j" >&2; exit 1; }
[ -s "$GRAPH" ] || { echo "missing graph $GRAPH (run ./scripts/verify.sh or dispatchlab import)" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/views" "$OUT/fonts" "$OUT/data"
"$DL" export-demo "$GRAPH" --out-dir "$OUT/data"
cp viewer/favicon.svg viewer/style.css viewer/core.js viewer/ui.js viewer/api.js viewer/map.js viewer/charts.js viewer/app.js "$OUT/"
cp viewer/views/*.js "$OUT/views/"
cp viewer/fonts/*.woff2 viewer/fonts/LICENSE-Geist.txt "$OUT/fonts/"

# Page body: the console's index.html without the document wrapper, with the
# offline bundles swapped for the recorded-demo manifest.
awk '
  /<!doctype|<html|<\/html>|<head>|<\/head>|<body>|<\/body>|<meta charset|<meta name="viewport"/ { next }
  /<title>/ { print "  <title>DispatchLab Demo</title>"; print "  <meta name=\"claude:viewport-fit-sides\" content=\"cover\">"; next }
  /Offline data, written by|src="replay_data.js"|src="results_data.js"/ { next }
  /<script src="core.js">/ {
    print "  <script>window.DISPATCHLAB_DEMO_BASE = \"data/\";</script>"
    print "  <script src=\"data/demo_meta.js\"></script>"
  }
  { print }
' viewer/index.html > "$OUT/index.html"
# The host frame sizes the page; fill it rather than the viewport.
sed -i.bak 's#<link rel="stylesheet" href="style.css">#<link rel="stylesheet" href="style.css">\n  <style>html, body { height: 100%; } body { background: var(--bg); } .app { height: 100%; }</style>#' "$OUT/index.html"
rm -f "$OUT/index.html.bak"
grep -q 'demo_meta.js' "$OUT/index.html" || { echo "page generation failed" >&2; exit 1; }

{
  echo '<!doctype html><html lang="en"><head><meta charset="utf-8">'
  echo '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"></head><body>'
  cat "$OUT/index.html"
  echo '</body></html>'
} > "$OUT/preview.html"

# Static site: the same files with a proper document (head elements in <head>).
SITE=site
rm -rf "$SITE"
mkdir -p "$SITE"
cp -R "$OUT/views" "$OUT/fonts" "$OUT/data" "$SITE/"
cp "$OUT"/*.js "$OUT"/*.css "$OUT"/favicon.svg "$SITE/"
{
  echo '<!doctype html>'
  echo '<html lang="en">'
  echo '<head>'
  echo '  <meta charset="utf-8">'
  echo '  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">'
  echo '  <meta name="description" content="DispatchLab: food-delivery routing and dispatch on the Tricity road network. Recorded demo of the C++20 engine.">'
  awk '/<div class="app"/ { exit } !/claude:viewport-fit-sides/ { print }' "$OUT/index.html"
  echo '</head>'
  echo '<body>'
  awk '/<div class="app"/ { body = 1 } body { print }' "$OUT/index.html"
  echo '</body>'
  echo '</html>'
} > "$SITE/index.html"
cat > "$SITE/vercel.json" <<'JSON'
{
  "headers": [
    { "source": "/fonts/(.*)", "headers": [{ "key": "Cache-Control", "value": "public, max-age=31536000, immutable" }] },
    { "source": "/data/(.*)", "headers": [{ "key": "Cache-Control", "value": "public, max-age=3600" }] }
  ]
}
JSON
grep -q '<div class="app"' "$SITE/index.html" && grep -q '<title>' "$SITE/index.html" || { echo "site generation failed" >&2; exit 1; }

echo "demo: $(find "$OUT" -type f | wc -l | tr -d ' ') files, $(du -sh "$OUT" | cut -f1) in $OUT"
echo "site: $(find "$SITE" -type f | wc -l | tr -d ' ') files, $(du -sh "$SITE" | cut -f1) in $SITE (deploy with Root Directory = site)"
echo "open $OUT/preview.html locally, or host the folder as static files"
