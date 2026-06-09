#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEFAULT_BIN="$ROOT_DIR/build-ninja/ninja-debug/examples/SAR/sar_example"
DEFAULT_PLUGIN_DIR="$ROOT_DIR/build-ninja/ninja-debug/examples/SAR/plugins"
NON_METAL_CFG="$ROOT_DIR/examples/SAR/config/sar_stripmap_definitive_nonmetal.json"
METAL_CFG="$ROOT_DIR/examples/SAR/config/sar_stripmap_definitive_metal.json"
RUNS=5

BIN_PATH="${1:-$DEFAULT_BIN}"
PLUGIN_DIR="${2:-$DEFAULT_PLUGIN_DIR}"

if [[ ! -x "$BIN_PATH" ]]; then
  echo "sar_example binary not found or not executable: $BIN_PATH" >&2
  exit 1
fi

if [[ ! -d "$PLUGIN_DIR" ]]; then
  echo "Plugin directory not found: $PLUGIN_DIR" >&2
  exit 1
fi

for cfg in "$NON_METAL_CFG" "$METAL_CFG"; do
  if [[ ! -f "$cfg" ]]; then
    echo "Config missing: $cfg" >&2
    exit 1
  fi
done

run_case() {
  local label="$1"
  local cfg="$2"

  echo "=== $label ==="
  local sum=0
  local min=999999999
  local max=0

  for i in $(seq 1 "$RUNS"); do
    local elapsed
    elapsed=$(/usr/bin/time -p "$BIN_PATH" "$cfg" "$PLUGIN_DIR" 2>&1 >/dev/null | awk '/^real / { printf("%.0f", $2 * 1000.0) }')
    echo "run_${i}_ms=$elapsed"

    sum=$((sum + elapsed))
    if (( elapsed < min )); then min=$elapsed; fi
    if (( elapsed > max )); then max=$elapsed; fi
  done

  local avg=$((sum / RUNS))
  echo "avg_ms=$avg"
  echo "min_ms=$min"
  echo "max_ms=$max"
  echo
}

run_case "non_metal" "$NON_METAL_CFG"
run_case "metal" "$METAL_CFG"

echo "Benchmark complete."
