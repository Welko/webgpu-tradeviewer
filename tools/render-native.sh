#!/usr/bin/env bash
# Draws every image-test scene through a real window with ViewerApp and saves
# renderings/native/<scene>.png, with its known pixels as <scene>.json and
# what drew it as about.json, for tools/compare-renderings.mjs, which checks
# them.
# Fails if a scene can't be drawn. CI runs it under xvfb-run, on lavapipe.
#
# Usage: tools/render-native.sh [ViewerApp] [output directory]
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
app=${1:-$root/cpp/build/native/apps/ViewerApp/ViewerApp}
out=${2:-${TRADEVIEWER_RENDERINGS_DIR:-$root/renderings}/native}
mkdir -p "$out"

scenes=()
while IFS= read -r scene; do
    scenes+=("$scene")
done < <("$app" --list-scenes)

failed=0
for scene in "${scenes[@]}"; do
    if ! log=$("$app" --scene="$scene" --snapshot="$out/$scene.png" 2>&1); then
        failed=1
    fi
    echo "$log" | grep -E "^(INFO|WARN|ERROR)" | grep -v "GPU timestamps" || true
done
exit $failed
