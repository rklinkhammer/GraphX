#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

DATASET_DIR="${1:-/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData}"
OUTPUT_DIR="${2:-/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output}"
COLLECTION_ID="${COLLECTION_ID:-gotcha-large-scene-subdata}"
MAX_MB="${MAX_MB:-256}"
BUILD_DIR="${GRAPHX_BUILD_DIR:-build-ninja/ninja-debug}"
GRAPHX_BIN="${GRAPHX_BIN:-${BUILD_DIR}/examples/SAR/graphx-gotcha-to-crsd}"
export GRAPHX_SAR_CRSD_WRITER="${GRAPHX_SAR_CRSD_WRITER:-${REPO_ROOT}/tools/sarpy/write_crsd_from_graphx_product.py}"

if [[ ! -d "${DATASET_DIR}" ]]; then
  echo "error: dataset directory not found: ${DATASET_DIR}" >&2
  exit 2
fi

bash scripts/prepare_gotcha_subdata_json.sh "${DATASET_DIR}"

if [[ ! -x "${GRAPHX_BIN}" ]]; then
  cmake --build "${BUILD_DIR}" --target graphx_gotcha_to_crsd -j8
fi

"${GRAPHX_BIN}" \
  --input-dir "${DATASET_DIR}" \
  --output-dir "${OUTPUT_DIR}" \
  --collection-id "${COLLECTION_ID}" \
  --max-output-size-mb "${MAX_MB}" \
  --sort manifest \
  --manifest "${DATASET_DIR}/manifest.json" \
  --mode crsd \
  --validate \
  --emit-index \
  --allow-classic-mat-with-sidecar

echo "conversion_output=${OUTPUT_DIR}"
echo "crsd_files:"
find "${OUTPUT_DIR}" -name 'product.crsd' -type f -print | sort
