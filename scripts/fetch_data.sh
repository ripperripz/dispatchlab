#!/usr/bin/env bash
# Reproduces data/raw/tricity-261017.osm.pbf from Geofabrik (optional: the
# clipped file is committed). Requires curl and osmium-tool. Network access is
# only needed here, never at runtime.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p data/raw
SRC="northern-zone-261017.osm.pbf"
URL="https://download.geofabrik.de/asia/india/$SRC"
if [ ! -f "data/raw/$SRC" ]; then
    curl -fL -o "data/raw/$SRC" "$URL"
    curl -fL -o "data/raw/$SRC.md5" "$URL.md5"
fi
(cd data/raw && if command -v md5sum >/dev/null; then md5sum -c "$SRC.md5"; else
    [ "$(md5 -q "$SRC")" = "$(cut -d' ' -f1 "$SRC.md5")" ] && echo "$SRC: OK"; fi)
osmium extract -b 76.62,30.62,76.95,30.80 --strategy complete_ways --set-bounds \
    -o data/raw/tricity-261017.osm.pbf --overwrite "data/raw/$SRC"
osmium fileinfo data/raw/tricity-261017.osm.pbf | head -20
