#!/usr/bin/env bash
# Benchmarks the working tree, uncommitted changes included, against the
# baseline's commit on this machine, as CI does (see "Memory and speed between
# commits" in README.md): builds the baseline's commit in a git worktree, runs
# both three times, taking turns to go first, and writes each one's medians:
#
#   benchmark/benchmark.json            this tree's, with its runs in benchmark/runs/
#   benchmark/baseline/benchmark.json   the baseline's, with its runs in benchmark/baseline/runs/
#
# The worktree is kept under cpp/build/baseline/<commit>, so later runs against
# the same commit reuse its build. On the web, until the baseline has a web row,
# there's no commit to build, and this tree runs alone. With --check, it then
# checks the medians as CI does; CI checks them in a step of its own, once
# they're uploaded.
#
# Usage: tools/benchmark-against-baseline.sh <native|web> <trades file> [--check]
#   native needs the native-release preset's tools; web needs Emscripten
#   (~/emsdk, or $EMSDK) and web/node_modules.
set -euo pipefail

usage() {
    echo "Usage: $0 <native|web> <trades file> [--check]" >&2
    exit 2
}

[[ $# -ge 2 ]] || usage
platform=$1
trades=$(realpath "$2")
check=false
[[ ${3:-} == --check ]] && check=true
[[ $platform == native || $platform == web ]] || usage

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
out=$root/benchmark
regressions=(node "$root/tools/check-regressions.mjs")

# CI names the commit it measures; a local run's benchmark.json counts as the
# commit checked out, -dirty with changes on top.
this_commit=()
[[ -n ${GITHUB_SHA:-} ]] && this_commit=(--commit "$GITHUB_SHA")

# The baseline's commit in its own worktree, or nothing if the baseline has no
# row for this platform.
baseline_tree=
if baseline_commit=$("${regressions[@]}" baseline-commit "$platform"); then
    baseline_tree=$root/cpp/build/baseline/$baseline_commit
    if [[ ! -d $baseline_tree ]]; then
        git worktree add --detach "$baseline_tree" "$baseline_commit"
    fi
elif [[ $platform == web ]]; then
    echo "No web row in the baseline yet: times won't be compared" >&2
else
    exit 1
fi

rm -rf "$out"
mkdir -p "$out/runs" "$out/baseline/runs"
servers=()
trap '[[ ${#servers[@]} -eq 0 ]] || kill "${servers[@]}" 2> /dev/null || true' EXIT

if [[ $platform == native ]]; then
    build() {
        (cd "$1/cpp" && cmake --preset native-release > /dev/null && cmake --build --preset native-release --target BenchmarkApp -j "$(nproc)")
    }
    build "$root"
    [[ -z $baseline_tree ]] || build "$baseline_tree"
    run_this() {
        "$root/cpp/build/native-release/apps/BenchmarkApp/BenchmarkApp" "$out/runs/$1.md" "$trades" "${this_commit[@]}"
    }
    run_baseline() {
        "$baseline_tree/cpp/build/native-release/apps/BenchmarkApp/BenchmarkApp" "$out/baseline/runs/$1.md" "$trades" \
            --commit "$baseline_commit"
    }
else
    command -v emcc > /dev/null || source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" > /dev/null
    (cd "$root/web" && npm run build)
    # The baseline's site, built once per commit.
    if [[ -n $baseline_tree && ! -f $baseline_tree/docs/benchmark.html ]]; then
        (cd "$baseline_tree/web" && npm ci && npm run build)
    fi
    # Each commit's site on a port of its own; vite directly, not through npx,
    # so the trap stops the server itself.
    serve() {
        (cd "$1/web" && exec node_modules/.bin/vite preview --port "$2" --strictPort > /dev/null) &
        servers+=("$!")
        for _ in $(seq 100); do
            curl -fs "http://localhost:$2/benchmark.html" > /dev/null && return
            sleep 0.2
        done
        echo "The site on port $2 never came up" >&2
        exit 1
    }
    serve "$root" 4173
    [[ -z $baseline_tree ]] || serve "$baseline_tree" 4174
    run_this() {
        node "$root/web/scripts/benchmark.mjs" "$out/runs/$1.md" "$trades" --url http://localhost:4173 "${this_commit[@]}"
    }
    run_baseline() {
        node "$root/web/scripts/benchmark.mjs" "$out/baseline/runs/$1.md" "$trades" --url http://localhost:4174 \
            --commit "$baseline_commit"
    }
fi

# Read once first, so no run reads it cold.
cat "$trades" > /dev/null
sides="this this this"
[[ -z $baseline_tree ]] || sides="baseline this this baseline baseline this"
this=0
other=0
for side in $sides; do
    if [[ $side == this ]]; then
        this=$((this + 1))
        run_this "$this"
    else
        other=$((other + 1))
        run_baseline "$other"
    fi
done

"${regressions[@]}" median "$out/benchmark.json" "$out"/runs/*.json
baseline=()
if [[ -n $baseline_tree ]]; then
    "${regressions[@]}" median "$out/baseline/benchmark.json" "$out"/baseline/runs/*.json
    baseline=(--baseline "$out/baseline/benchmark.json")
fi

if $check; then
    "${regressions[@]}" check "$out/benchmark.json" "${baseline[@]}"
fi
