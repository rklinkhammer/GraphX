#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

DATASET_DIR="${1:-/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData}"
OUTPUT_DIR="${2:-/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output}"
COLLECTION_ID="${COLLECTION_ID:-gotcha-large-scene-subdata}"
MAX_MB="${MAX_MB:-0}"
BUILD_DIR="${GRAPHX_BUILD_DIR:-}"
GRAPHX_BIN="${GRAPHX_BIN:-}"
export GRAPHX_SAR_CRSD_WRITER="${GRAPHX_SAR_CRSD_WRITER:-${REPO_ROOT}/tools/sarpy/write_crsd_from_graphx_product.py}"
SORT_MODE="${SORT_MODE:-lexical}"
if [[ -n "${MANIFEST_PATH:-}" ]]; then
  MANIFEST_PATH="${MANIFEST_PATH}"
  MANIFEST_PATH_IS_DEFAULT=0
else
  MANIFEST_PATH="${DATASET_DIR}/manifest.json"
  MANIFEST_PATH_IS_DEFAULT=1
fi
ENABLE_VALIDATE="${ENABLE_VALIDATE:-0}"
ENABLE_EMIT_INDEX="${ENABLE_EMIT_INDEX:-0}"
PREPROCESS_CLASSIC_MAT="${PREPROCESS_CLASSIC_MAT:-1}"
PREPROCESS_OUTPUT_DIR="${PREPROCESS_OUTPUT_DIR:-}"
REQUIRE_HDF5_CONVERTER=0

pick_configured_build_dir() {
  local dir=""
  for dir in \
    "build-ninja/ninja-debug-metal-native" \
    "build-ninja/ninja-debug" \
    "build"; do
    if [[ -f "${dir}/CMakeCache.txt" ]]; then
      printf '%s\n' "${dir}"
      return 0
    fi
  done
  return 1
}

pick_existing_converter_bin() {
  local bin=""
  for bin in \
    "build-ninja/ninja-debug-metal-native/examples/SAR/graphx-gotcha-to-crsd" \
    "build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd" \
    "build/examples/SAR/graphx-gotcha-to-crsd"; do
    if [[ -x "${bin}" ]]; then
      printf '%s\n' "${bin}"
      return 0
    fi
  done
  return 1
}

build_has_hdf5() {
  local dir="$1"
  local cache="${dir}/CMakeCache.txt"
  local ninja="${dir}/build.ninja"
  local include_line=""

  if [[ -f "${ninja}" ]] && grep -q 'GRAPHX_SAR_HAS_HDF5=1' "${ninja}"; then
    return 0
  fi

  if [[ ! -f "${cache}" ]]; then
    return 1
  fi
  include_line="$(grep '^HDF5_C_INCLUDE_DIR:PATH=' "${cache}" 2>/dev/null || true)"
  [[ -n "${include_line}" && "${include_line}" != *"NOTFOUND" ]]
}

ensure_hdf5_in_build_dir() {
  local dir="$1"
  local hdf5_root="${HDF5_ROOT:-}"

  if build_has_hdf5 "${dir}"; then
    return 0
  fi

  if [[ -z "${hdf5_root}" ]] && command -v brew >/dev/null 2>&1; then
    hdf5_root="$(brew --prefix hdf5 2>/dev/null || true)"
  fi

  if [[ -z "${hdf5_root}" && -d "/opt/homebrew/opt/hdf5" ]]; then
    hdf5_root="/opt/homebrew/opt/hdf5"
  fi

  if [[ -z "${hdf5_root}" && -d "/usr/local/opt/hdf5" ]]; then
    hdf5_root="/usr/local/opt/hdf5"
  fi

  if [[ -z "${hdf5_root}" || ! -d "${hdf5_root}" ]]; then
    echo "error: HDF5 is required for MAT preprocessing lane but was not found" >&2
    echo "hint: install hdf5 (for example, 'brew install hdf5') or set HDF5_ROOT" >&2
    return 1
  fi

  echo "info: reconfiguring ${dir} with HDF5_ROOT=${hdf5_root}" >&2
  cmake -S "${REPO_ROOT}" -B "${dir}" -DHDF5_ROOT="${hdf5_root}"

  if ! build_has_hdf5 "${dir}"; then
    echo "error: HDF5 still not detected after reconfigure in ${dir}" >&2
    return 1
  fi
  return 0
}

if [[ ! -d "${DATASET_DIR}" ]]; then
  echo "error: dataset directory not found: ${DATASET_DIR}" >&2
  exit 2
fi

if ! find "${DATASET_DIR}" -maxdepth 1 -type f -name '*.mat' -print -quit | grep -q .; then
  echo "error: no .mat files found in dataset directory: ${DATASET_DIR}" >&2
  exit 3
fi

if command -v file >/dev/null 2>&1; then
  first_mat="$(find "${DATASET_DIR}" -maxdepth 1 -type f -name '*.mat' | sort | head -n1 || true)"
  if [[ -n "${first_mat}" ]]; then
    mat_desc="$(file "${first_mat}" 2>/dev/null || true)"
    if [[ "${mat_desc}" == *"Matlab v5 mat-file"* ]]; then
      if [[ "${PREPROCESS_CLASSIC_MAT}" != "1" ]]; then
        echo "error: classic MATLAB MAT v5 detected: ${first_mat}" >&2
        echo "error: graphx-gotcha-to-crsd currently supports HDF5 MAT v7.3 inputs only" >&2
        echo "hint: set PREPROCESS_CLASSIC_MAT=1 (default) to auto-convert classic MAT to HDF5 MAT" >&2
        exit 6
      fi

      if ! command -v python3 >/dev/null 2>&1; then
        echo "error: classic MATLAB MAT v5 detected but python3 is not available for preprocessing" >&2
        exit 7
      fi

      preprocess_tool="${REPO_ROOT}/tools/sarpy/convert_gotcha_classic_mat_to_v73.py"
      if [[ ! -f "${preprocess_tool}" ]]; then
        echo "error: preprocessing tool not found: ${preprocess_tool}" >&2
        exit 8
      fi

      if [[ -z "${PREPROCESS_OUTPUT_DIR}" ]]; then
        PREPROCESS_OUTPUT_DIR="${OUTPUT_DIR}/_preprocessed_hdf5_mat"
      fi

      echo "info: classic MATLAB MAT v5 detected; preprocessing to HDF5 MAT v7.3-compatible files" >&2
      python3 "${preprocess_tool}" \
        --input-dir "${DATASET_DIR}" \
        --output-dir "${PREPROCESS_OUTPUT_DIR}" \
        --overwrite

      DATASET_DIR="${PREPROCESS_OUTPUT_DIR}"
      REQUIRE_HDF5_CONVERTER=1
      if [[ "${MANIFEST_PATH_IS_DEFAULT}" == "1" ]]; then
        MANIFEST_PATH="${DATASET_DIR}/manifest.json"
      fi
      echo "info: using preprocessed dataset directory: ${DATASET_DIR}" >&2
    fi
  fi
fi

if [[ -z "${BUILD_DIR}" ]]; then
  BUILD_DIR="$(pick_configured_build_dir || true)"
fi

if [[ -z "${GRAPHX_BIN}" ]]; then
  if [[ -n "${BUILD_DIR}" ]]; then
    GRAPHX_BIN="${BUILD_DIR}/examples/SAR/graphx-gotcha-to-crsd"
  else
    GRAPHX_BIN="$(pick_existing_converter_bin || true)"
  fi
fi

if [[ "${REQUIRE_HDF5_CONVERTER}" == "1" ]]; then
  if [[ -z "${BUILD_DIR}" && -n "${GRAPHX_BIN}" ]]; then
    BUILD_DIR="$(dirname "$(dirname "$(dirname "${GRAPHX_BIN}")")")"
  fi
  if [[ -z "${BUILD_DIR}" || ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "error: unable to determine a valid CMake build directory for HDF5-enabled converter" >&2
    echo "hint: set GRAPHX_BUILD_DIR to a configured build tree" >&2
    exit 9
  fi
  if ! ensure_hdf5_in_build_dir "${BUILD_DIR}"; then
    exit 10
  fi
  cmake --build "${BUILD_DIR}" --target graphx_gotcha_to_crsd -j8
  GRAPHX_BIN="${BUILD_DIR}/examples/SAR/graphx-gotcha-to-crsd"
fi

if [[ ! -x "${GRAPHX_BIN}" ]]; then
  if [[ -n "${BUILD_DIR}" && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cmake --build "${BUILD_DIR}" --target graphx_gotcha_to_crsd -j8
    GRAPHX_BIN="${BUILD_DIR}/examples/SAR/graphx-gotcha-to-crsd"
  else
    cmake --build --preset build-sar-example-unit
    GRAPHX_BIN="$(pick_existing_converter_bin || true)"
  fi
fi

if [[ ! -x "${GRAPHX_BIN}" ]]; then
  echo "error: graphx-gotcha-to-crsd executable not found after build attempt" >&2
  echo "hint: set GRAPHX_BIN or GRAPHX_BUILD_DIR explicitly" >&2
  exit 5
fi

sort_args=(--sort "${SORT_MODE}")
if [[ "${SORT_MODE}" == "manifest" ]]; then
  if [[ ! -f "${MANIFEST_PATH}" ]]; then
    echo "error: manifest sort requested but manifest file not found: ${MANIFEST_PATH}" >&2
    exit 4
  fi
  sort_args+=(--manifest "${MANIFEST_PATH}")
fi

cmd=(
  "${GRAPHX_BIN}"
  --input-dir "${DATASET_DIR}"
  --output-dir "${OUTPUT_DIR}"
  --collection-id "${COLLECTION_ID}"
  --max-output-size-mb "${MAX_MB}"
  --mode crsd
  "${sort_args[@]}"
)

if [[ "${ENABLE_VALIDATE}" == "1" ]]; then
  cmd+=(--validate)
fi
if [[ "${ENABLE_EMIT_INDEX}" == "1" ]]; then
  cmd+=(--emit-index)
fi

"${cmd[@]}"

echo "conversion_output=${OUTPUT_DIR}"
echo "crsd_files:"
find "${OUTPUT_DIR}" -name 'product.crsd' -type f -print | sort
