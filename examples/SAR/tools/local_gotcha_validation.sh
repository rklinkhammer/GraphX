#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

if [[ -z "${GRAPHX_SAR_GOTCHA_DATASET:-}" ]]; then
  echo "error: GRAPHX_SAR_GOTCHA_DATASET must be set to a local GOTCHA .mat directory" >&2
  exit 2
fi

dataset_root="${GRAPHX_SAR_GOTCHA_DATASET}"
if [[ ! -d "${dataset_root}" ]]; then
  echo "error: GRAPHX_SAR_GOTCHA_DATASET does not exist or is not a directory: ${dataset_root}" >&2
  exit 3
fi

if ! find "${dataset_root}" -maxdepth 1 -type f -name '*.mat' -print -quit | grep -q .; then
  echo "error: GRAPHX_SAR_GOTCHA_DATASET contains no top-level .mat files: ${dataset_root}" >&2
  exit 4
fi

bash "${repo_root}/scripts/verify_gotcha_dataset.sh" >/dev/null

runner_bin="${GRAPHX_SAR_GOTCHA_TO_CRSD_BIN:-${repo_root}/build-ninja/ninja-debug/examples/SAR/graphx-gotcha-to-crsd}"
if [[ ! -x "${runner_bin}" ]]; then
  echo "error: graphx-gotcha-to-crsd executable not found or not executable: ${runner_bin}" >&2
  echo "hint: set GRAPHX_SAR_GOTCHA_TO_CRSD_BIN or build the SAR example target" >&2
  exit 5
fi

output_dir="${GRAPHX_SAR_GOTCHA_OUTPUT_DIR:-${TMPDIR:-/tmp}/graphx_sar_real_gotcha_validation}"
collection_id="${GRAPHX_SAR_GOTCHA_COLLECTION_ID:-local-real-gotcha}"
max_output_size_mb="${GRAPHX_SAR_GOTCHA_MAX_OUTPUT_SIZE_MB:-512}"
manifest_path="${GRAPHX_SAR_GOTCHA_MANIFEST:-${dataset_root}/manifest.json}"

rm -rf "${output_dir}"
mkdir -p "${output_dir}"

"${runner_bin}" \
  --input-dir "${dataset_root}" \
  --output-dir "${output_dir}" \
  --collection-id "${collection_id}" \
  --max-output-size-mb "${max_output_size_mb}" \
  --sort manifest \
  --manifest "${manifest_path}" \
  --mode graphx-sar-normalized \
  --validate \
  --emit-index \
  --allow-classic-mat-with-sidecar

test -f "${output_dir}/gotcha_sar_normalized_index.json"
test -f "${output_dir}/conversion_report.json"
test -f "${output_dir}/conversion_warnings.log"

echo "local_gotcha_validation_ok"
echo "dataset_root=${dataset_root}"
echo "output_dir=${output_dir}"
