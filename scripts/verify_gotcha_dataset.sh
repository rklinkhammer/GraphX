#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${GRAPHX_SAR_GOTCHA_DATASET:-}" ]]; then
  echo "error: GRAPHX_SAR_GOTCHA_DATASET must be set" >&2
  exit 2
fi

dataset_root="${GRAPHX_SAR_GOTCHA_DATASET}"
manifest_path="${GRAPHX_SAR_GOTCHA_MANIFEST:-${dataset_root}/manifest.json}"
checksums_path="${GRAPHX_SAR_GOTCHA_CHECKSUMS:-${dataset_root}/checksums.sha256}"

if [[ ! -d "${dataset_root}" ]]; then
  echo "error: dataset root does not exist: ${dataset_root}" >&2
  exit 3
fi

if [[ ! -f "${manifest_path}" ]]; then
  echo "error: manifest file not found: ${manifest_path}" >&2
  exit 4
fi
if [[ ! -s "${manifest_path}" ]]; then
  echo "error: manifest file is empty: ${manifest_path}" >&2
  exit 5
fi

if [[ ! -f "${checksums_path}" ]]; then
  echo "error: checksum file not found: ${checksums_path}" >&2
  exit 6
fi
if [[ ! -s "${checksums_path}" ]]; then
  echo "error: checksum file is empty: ${checksums_path}" >&2
  exit 7
fi

if ! command -v shasum >/dev/null 2>&1; then
  echo "error: shasum command not found (required for checksum verification)" >&2
  exit 8
fi

line_number=0
verified=0
while IFS= read -r line || [[ -n "${line}" ]]; do
  line_number=$((line_number + 1))

  # Skip comments and blank lines.
  if [[ -z "${line}" || "${line}" == \#* ]]; then
    continue
  fi

  expected_hash="$(awk '{print $1}' <<<"${line}")"
  relative_path="$(awk '{print $2}' <<<"${line}")"

  if [[ -z "${expected_hash}" || -z "${relative_path}" ]]; then
    echo "error: malformed checksum entry at line ${line_number}: ${line}" >&2
    exit 9
  fi

  file_path="${dataset_root}/${relative_path}"
  if [[ ! -f "${file_path}" ]]; then
    echo "error: checksum target file missing: ${file_path}" >&2
    exit 10
  fi

  computed_hash="$(shasum -a 256 "${file_path}" | awk '{print $1}')"
  if [[ "${computed_hash}" != "${expected_hash}" ]]; then
    echo "error: checksum mismatch for ${relative_path}" >&2
    echo "expected: ${expected_hash}" >&2
    echo "computed: ${computed_hash}" >&2
    exit 11
  fi

  verified=$((verified + 1))
done < "${checksums_path}"

if [[ ${verified} -eq 0 ]]; then
  echo "error: no checksum entries were verified in ${checksums_path}" >&2
  exit 12
fi

echo "dataset_preflight_ok"
echo "dataset_root=${dataset_root}"
echo "manifest_path=${manifest_path}"
echo "checksums_path=${checksums_path}"
echo "files_verified=${verified}"
