#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

DATASET_DIR="${1:-/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData}"
OUTPUT_DIR="${2:-/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output}"
COLLECTION_ID="${COLLECTION_ID:-gotcha-large-scene-subdata}"
MAX_MB="${MAX_MB:-0}"
BUILD_DIR="${GRAPHX_BUILD_DIR:-build-ninja/ninja-debug}"
GRAPHX_BIN="${GRAPHX_BIN:-${BUILD_DIR}/examples/SAR/graphx-gotcha-to-crsd}"
export GRAPHX_SAR_CRSD_WRITER="${GRAPHX_SAR_CRSD_WRITER:-${REPO_ROOT}/tools/sarpy/write_crsd_from_graphx_product.py}"
SORT_MODE="${SORT_MODE:-lexical}"
MANIFEST_PATH="${MANIFEST_PATH:-${DATASET_DIR}/manifest.json}"
ENABLE_VALIDATE="${ENABLE_VALIDATE:-0}"
ENABLE_EMIT_INDEX="${ENABLE_EMIT_INDEX:-0}"

if [[ ! -d "${DATASET_DIR}" ]]; then
  echo "error: dataset directory not found: ${DATASET_DIR}" >&2
  exit 2
fi

if ! find "${DATASET_DIR}" -maxdepth 1 -type f -name '*.mat' -print -quit | grep -q .; then
  echo "error: no .mat files found in dataset directory: ${DATASET_DIR}" >&2
  exit 3
fi

if [[ ! -x "${GRAPHX_BIN}" ]]; then
  cmake --build "${BUILD_DIR}" --target graphx_gotcha_to_crsd -j8
fi

sort_args=(--sort "${SORT_MODE}")
if [[ "${SORT_MODE}" == "manifest" ]]; then
  if [[ ! -f "${MANIFEST_PATH}" ]]; then
    echo "error: manifest sort requested but manifest file not found: ${MANIFEST_PATH}" >&2
    exit 4
  fi
  sort_args+=(--manifest "${MANIFEST_PATH}")
fi

extra_args=()
if [[ "${ENABLE_VALIDATE}" == "1" ]]; then
  extra_args+=(--validate)
fi
if [[ "${ENABLE_EMIT_INDEX}" == "1" ]]; then
  extra_args+=(--emit-index)
fi

"${GRAPHX_BIN}" \
  --input-dir "${DATASET_DIR}" \
  --output-dir "${OUTPUT_DIR}" \
  --collection-id "${COLLECTION_ID}" \
  --max-output-size-mb "${MAX_MB}" \
  --mode crsd \
  "${sort_args[@]}" \
  "${extra_args[@]}"

echo "conversion_output=${OUTPUT_DIR}"
echo "crsd_files:"
find "${OUTPUT_DIR}" -name 'product.crsd' -type f -print | sort
