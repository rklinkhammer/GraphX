> ARCHIVAL STATUS (2026-06-14): This document is kept for historical traceability. It may reference deprecated GraphX SAR conversion lanes, flags, or scripts. Use the active CRSD-only workflow in plan/prompt examples/doc.md and scripts/convert_gotcha_subdata_to_crsd.sh.

# SAR GOTCHA Local Validation Fix Implementer Report

Date: 2026-06-14
Role: IMPLEMENTER
Task: Fix local-only real GOTCHA validation lane for
`/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData`

## 1. Files Changed

- `examples/SAR/tools/local_gotcha_validation.sh`
  - Invokes `scripts/verify_gotcha_dataset.sh` through `bash` so the lane does
    not depend on executable mode for the preflight script.
  - Passes `--allow-classic-mat-with-sidecar` to `graphx-gotcha-to-crsd`, which
    matches the sidecar-based local GOTCHA workflow.

- `tools/sarpy/generate_gotcha_subdata_sidecars.py`
  - Generated sidecars now include the required raw GOTCHA validation fields:
    `Np`, `K`, `deltaF`, `minF`, `AntX`, `AntY`, `AntZ`, `R0`, and `phdata`.
  - Existing normalized fields used by `GotchaMatReader` are preserved.
  - Manifest and checksum generation are unchanged.
  - `Np` is emitted as `1` because the current generator exports one selected
    pulse per MAT file into `iq_samples`.

- `tools/sarpy/test_generate_gotcha_subdata_sidecars.py`
  - Added a focused Python unit test using a tiny synthetic MAT file.
  - Verifies required raw fields, normalized fields, manifest generation, and
    checksum generation.

- `plan/reviews/SAR_GOTCHA_LOCAL_VALIDATION_FIX_IMPLEMENTER_REPORT.md`
  - This report.

## 2. Files Deleted

- None.

## 3. Tests Added

- `tools/sarpy/test_generate_gotcha_subdata_sidecars.py`

## 4. Tests Removed

- None.

## 5. Build/Test Commands

- `python3 tools/sarpy/test_generate_gotcha_subdata_sidecars.py`
  - Result: **PASS**
  - `Ran 1 test`

- `bash scripts/prepare_gotcha_subdata_json.sh /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData`
  - Result: **PASS**
  - `mat_files: 10`
  - `sidecars_written: 10`
  - Generated `manifest.json` and `checksums.sha256` beside the local dataset.

- `GRAPHX_SAR_GOTCHA_DATASET=/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData bash scripts/verify_gotcha_dataset.sh`
  - Result: **PASS**
  - `files_verified=10`

- `cmake --build build-ninja/ninja-debug-metal-native --target graphx_gotcha_to_crsd test_sar_example_unit`
  - Result: **PASS**

- `GRAPHX_SAR_GOTCHA_DATASET=/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData GRAPHX_SAR_GOTCHA_TO_CRSD_BIN=/Users/rklinkhammer/workspace/GraphX/build-ninja/ninja-debug-metal-native/examples/SAR/graphx-gotcha-to-crsd GRAPHX_SAR_GOTCHA_OUTPUT_DIR=/private/tmp/graphx_sar_real_gotcha_validation ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit '--gtest_filter=LocalGotchaValidationLaneTest.*'`
  - Result: **PASS**
  - `2 tests from 1 test suite ran`
  - `2 passed`

## 6. Local Output Validated

Output directory:

`/private/tmp/graphx_sar_real_gotcha_validation`

Observed required outputs:

- `gotcha_crsd_index.json`
- `conversion_report.json`
- `conversion_warnings.log`
- `gotcha_crsd_chunk_0000.graphx-crsd-lite/signal.bin`
- `gotcha_crsd_chunk_0000.graphx-crsd-lite/metadata.json`
- `gotcha_crsd_chunk_0000.graphx-crsd-lite/index.json`
- `gotcha_crsd_chunk_0000.graphx-crsd-lite/conversion_report.json`
- `gotcha_crsd_chunk_0000.graphx-crsd-lite/conversion_warnings.log`

Top-level `conversion_report.json` reports:

- `format: graphx-crsd-lite`
- `label: NON-STANDARD`
- `selected_mode: graphx-crsd-lite`
- `source_ordering: manifest`
- `validation_status: ok`
- `warnings: []`

Top-level `gotcha_crsd_index.json` reports:

- `schema: graphx.sar.gotcha_crsd_index.v1`
- `collection_id: local-real-gotcha`
- `source_files: 10`
- `outputs: 1`
- `source_ordering: manifest`

## 7. Remaining Follow-Up Work

- This validates the existing local-only `graphx-crsd-lite` lane, not
  standards-compliant CRSD or SarPy image correctness.
- The current sidecar generator exports one selected pulse per MAT file. Full
  multi-pulse real GOTCHA sidecar emission remains separate future work.
- Generated real-data sidecars, manifest, checksums, and output artifacts were
  kept outside the repository.
