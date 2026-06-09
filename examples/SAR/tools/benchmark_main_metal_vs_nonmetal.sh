#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEFAULT_BIN="$ROOT_DIR/build-ninja/ninja-debug/examples/SAR/sar_example"
DEFAULT_PLUGIN_DIR="$ROOT_DIR/build-ninja/ninja-debug/examples/SAR/plugins"
DEFINITIVE_CFG="$ROOT_DIR/examples/SAR/config/sar_stripmap_definitive.json"
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

if [[ ! -f "$DEFINITIVE_CFG" ]]; then
  echo "Config missing: $DEFINITIVE_CFG" >&2
  exit 1
fi

make_backend_variant() {
  local source_cfg="$1"
  local backend="$2"
  local output_tmp
  local output_cfg
  output_tmp="$(mktemp "/tmp/sar_stripmap_definitive_${backend}.XXXXXX")"
  output_cfg="${output_tmp}.json"
  mv "$output_tmp" "$output_cfg"

  sed -E "s/(\"execution_backend\"[[:space:]]*:[[:space:]]*\")[^\"]+(\",)/\\1${backend}\\2/" "$source_cfg" > "$output_cfg"
  if [[ "$backend" == "metal" ]]; then
    sed -i '' -E "s/(\"backend_fallback_policy\"[[:space:]]*:[[:space:]]*\")[^\"]+(\",)/\\1allow_fallback\\2/" "$output_cfg"
  fi
  printf '%s\n' "$output_cfg"
}

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
  CASE_AVG="$avg"
}

STUB_CFG_RUNTIME="$(make_backend_variant "$DEFINITIVE_CFG" "stub")"
METAL_CFG_RUNTIME="$(make_backend_variant "$DEFINITIVE_CFG" "metal")"
trap 'rm -f "$STUB_CFG_RUNTIME" "$METAL_CFG_RUNTIME"' EXIT

CASE_AVG=0
run_case "resolver_stub" "$STUB_CFG_RUNTIME"
stub_avg="$CASE_AVG"

run_case "resolver_metal" "$METAL_CFG_RUNTIME"
metal_avg="$CASE_AVG"

if (( metal_avg < stub_avg )); then
  improvement=$((stub_avg - metal_avg))
  improvement_pct=$((improvement * 100 / stub_avg))
  echo "metal_improvement_ms=$improvement"
  echo "metal_improvement_percent=${improvement_pct}%"
else
  regression=$((metal_avg - stub_avg))
  regression_pct=$((regression * 100 / stub_avg))
  echo "metal_regression_ms=$regression"
  echo "metal_regression_percent=${regression_pct}%"
fi

echo "Benchmark complete."
