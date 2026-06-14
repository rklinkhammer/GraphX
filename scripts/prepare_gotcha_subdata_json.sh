#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

DATASET_DIR="${1:-/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData}"
PULSE_INDEX="${PULSE_INDEX:-0}"

python3 tools/sarpy/generate_gotcha_subdata_sidecars.py \
  --input-dir "${DATASET_DIR}" \
  --pulse-index "${PULSE_INDEX}" \
  --overwrite

echo "prepared_dataset_json=${DATASET_DIR}"
echo "manifest=${DATASET_DIR}/manifest.json"
echo "checksums=${DATASET_DIR}/checksums.sha256"
