> ARCHIVAL STATUS (2026-06-14): This document is kept for historical traceability. It may reference deprecated GraphX SAR conversion lanes, flags, or scripts. Use the active CRSD-only workflow in plan/prompt examples/doc.md and scripts/convert_gotcha_subdata_to_crsd.sh.

# SAR Rename CRSD-Lite Implementer Report

Date: 2026-06-14
Role: IMPLEMENTER
Task: Rename `graphx-crsd-lite` to `graphx-sar-normalized`

## 1. Files Changed

- `examples/SAR/include/sar/io/GraphxSarNormalizedIO.hpp`
  - Renamed from `GraphxCrsdLiteIO.hpp`.
  - Renamed public C++ symbols to `GraphxSarNormalizedOptions`,
    `GraphxSarNormalizedWriter`, and `GraphxSarNormalizedReader`.
  - Format name is now `graphx-sar-normalized`.
  - Schemas now use `graphx_sar_normalized`.
  - Retains `NON-STANDARD` labeling and checksum/report behavior.

- `examples/SAR/src/graphx_gotcha_to_crsd.cpp`
  - Intermediate CLI mode is now `--mode graphx-sar-normalized`.
  - Removed old mode alias; backward compatibility is not preserved.
  - CLI help states `graphx-sar-normalized is NON-STANDARD and is not CRSD`.
  - Normalized output directories now use
    `gotcha_sar_normalized_chunk_*.graphx-sar-normalized/`.
  - Normalized root index is now `gotcha_sar_normalized_index.json`.
  - `--mode crsd` remains separate and still emits CRSD-named artifacts.

- `examples/SAR/include/sar/io/SarIoUtilities.hpp`
  - Renamed the generic package index helper from `BuildLiteIndexJson` to
    `BuildSarPackageIndexJson`.
  - Renamed GOTCHA output index inputs/helper from CRSD-specific names to
    `GotchaOutputIndexBuildInput` and `BuildGotchaOutputIndexJson`.

- `examples/SAR/include/sar/io/GotchaMatInspector.hpp`
  - Renamed inspection assumption key to `graphx_sar_normalized_emitted`.
  - Replaced the old no-lite assumption string with
    `no_intermediate_or_crsd_output`.

- `examples/SAR/test/test_graphx_sar_normalized_io.cpp`
  - Renamed from `test_graphx_crsd_lite_io.cpp`.
  - Updated test fixture/class/symbol names and expected format strings.

- `examples/SAR/test/test_graphx_sar_normalized_lane.cpp`
  - Renamed from `test_graphx_crsd_lite_lane.cpp`.
  - Updated end-to-end normalized artifact names and deterministic checks.

- `examples/SAR/test/test_graphx_gotcha_to_crsd_cli.cpp`
  - Updated intermediate mode tests to `graphx-sar-normalized`.
  - Added CLI help guardrail assertion for `NON-STANDARD` and `not CRSD`.
  - Preserved CRSD-mode checks for `gotcha_crsd_*` outputs.

- `examples/SAR/test/test_local_gotcha_validation_lane.cpp`
  - Updated local-only lane expectations to the new mode and index name.

- `examples/SAR/test/test_sar_io_utilities.cpp`
  - Updated helper names, schema expectations, and normalized output names.

- `examples/SAR/test/test_gotcha_mat_inspector.cpp`
  - Updated inspection assumption key expectation.

- `examples/SAR/test/CMakeLists.txt`
  - Updated renamed test source filenames.

- `examples/SAR/tools/local_gotcha_validation.sh`
  - Uses `--mode graphx-sar-normalized`.
  - Verifies `gotcha_sar_normalized_index.json`.

- `examples/SAR/tools/local_gotcha_validation.md`
  - Updated local-only workflow docs and output artifact names.

- `scripts/convert_gotcha_subdata_to_graphx_sar_normalized.sh`
  - Renamed from `convert_gotcha_subdata_to_graphx_crsd_lite.sh`.
  - Uses `--mode graphx-sar-normalized`.

- `README.md`
- `docs/CONSOLIDATED_OPERATIONS.md`
- `docs/sar/crsd_definition.md`
- `docs/sar/gotcha_crsd_repo_discovery.md`
- `docs/sar/gotcha_input_manifest_schema.md`
- `docs/sar/gotcha_template.md`
- `tools/CRSD_Convert.md`
- `tools/LSD_Convert.md`
  - Updated active user-facing terminology and examples.
  - Removed stale embedded prompt text from `docs/sar/gotcha_template.md`.

## 2. Files Deleted

- `examples/SAR/include/sar/io/GraphxCrsdLiteIO.hpp`
- `examples/SAR/test/test_graphx_crsd_lite_io.cpp`
- `examples/SAR/test/test_graphx_crsd_lite_lane.cpp`
- `scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh`

## 3. Tests Added

- No net-new test coverage area was added; renamed tests continue to cover the
  same behavior under `GraphxSarNormalized*`.
- Added a narrow CLI help assertion that the intermediate format is
  `NON-STANDARD` and is not CRSD.

## 4. Tests Removed

- No behavior coverage was removed. Old filename/symbol tests were renamed.

## 5. Build/Test Commands

- `cmake --build build-ninja/ninja-debug-metal-native --target graphx_gotcha_to_crsd test_sar_example_unit`
  - Result: **PASS**

- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit '--gtest_filter=*GraphxSarNormalized*:GraphxGotchaToCrsdCliTest.*:LocalGotchaValidationLaneTest.RunnerIsExplicitlyGatedAndDocumentsLocalOnlyBoundaries:SarIoUtilitiesTest.*:GotchaMatInspectorTest.ConversionAssumptionsReportHasStableInspectionOnlyShape'`
  - Result: **PASS**
  - `15 tests from 6 test suites ran`
  - `15 passed`

- `rg "graphx-crsd-lite|GraphxCrsdLite|crsd-lite|CrsdLite" examples/SAR docs tools scripts CMakeLists.txt cmake README.md`
  - Result: **PASS**
  - No active matches.

- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit`
  - Result: **PASS**
  - `240 tests from 53 test suites ran`
  - `237 passed`
  - `3 skipped`

- `GRAPHX_SAR_GOTCHA_DATASET=/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData GRAPHX_SAR_GOTCHA_TO_CRSD_BIN=/Users/rklinkhammer/workspace/GraphX/build-ninja/ninja-debug-metal-native/examples/SAR/graphx-gotcha-to-crsd GRAPHX_SAR_GOTCHA_OUTPUT_DIR=/private/tmp/graphx_sar_real_gotcha_validation ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit '--gtest_filter=LocalGotchaValidationLaneTest.*'`
  - Result: **PASS**
  - `2 tests from 1 test suite ran`
  - `2 passed`

## 6. Remaining Follow-Up Work

- No compatibility alias for `graphx-crsd-lite` was kept.
- `--mode crsd` remains the standards-targeted CRSD path.
- `graphx-sar-normalized` remains a GraphX-owned non-standard intermediate
  artifact format and is explicitly labeled as such.
- Real GOTCHA validation remains local-only and gated by
  `GRAPHX_SAR_GOTCHA_DATASET`.
