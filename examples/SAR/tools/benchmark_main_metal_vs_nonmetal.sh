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
  local output_cfg
  output_cfg="$(mktemp "/tmp/sar_stripmap_definitive_${backend}.XXXXXX.json")"

  sed -E "s/(\"execution_backend\"[[:space:]]*:[[:space:]]*\")[^\"]+(\",)/\\1${backend}\\2/" "$source_cfg" > "$output_cfg"
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
}

METAL_CFG_RUNTIME="$(make_backend_variant "$DEFINITIVE_CFG" "metal")"
trap 'rm -f "$METAL_CFG_RUNTIME"' EXIT

run_case "resolver_auto" "$DEFINITIVE_CFG"
run_case "resolver_metal" "$METAL_CFG_RUNTIME"

echo "Benchmark complete."
