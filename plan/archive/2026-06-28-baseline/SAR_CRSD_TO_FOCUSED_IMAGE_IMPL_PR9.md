# SAR CRSD To Focused Image IMPLEMENTER Report - PR9

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR9 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Local-only GOTCHA-derived CRSD validation workflow

## Summary

Implemented a PR9 local-only, opt-in GOTCHA-derived CRSD validation workflow with:

- dedicated local validation topology config for ordered `subData01..subData10` CRSD products
- disabled-by-default, env-gated PR9 validation lane test
- documentation updates for workflow usage and boundaries
- conversion-script support artifact (`ordered_crsd_set_report.json`) with ordered-set checksum and per-segment checksums

This implementation keeps CI independent of real GOTCHA data and SarPy, and introduces no MATLAB dependency.

## Scope Coverage

1. Add/update local-only workflow, docs, config, and disabled-by-default tests.
- Added config: `examples/SAR/config/sar_crsd_gotcha_local_validation.json`.
- Added test: `examples/SAR/test/test_local_gotcha_validation_lane.cpp`.
- Wired test in `examples/SAR/test/CMakeLists.txt` and reused existing disabled local lane target `sar_real_gotcha_local_validation`.
- Updated docs:
  - `docs/sar/gotcha_large_scene_data_description.md`
  - `docs/CONSOLIDATED_OPERATIONS.md`

2. Workflow operates on generated CRSD layout (`subData01..subData10 .../product.crsd`).
- PR9 test builds ordered path list exactly for:
  - `subData01.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd`
  - ...
  - `subData10.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd`
- Paths are treated as one ordered aperture set in the source-node config.

3. Produce one final focused-image artifact set.
- PR9 test executes `sar_example` with local config and asserts exactly one `.bin`, one `.json`, and one `.pgm` focused-image artifact set is emitted.

4. Verify nonzero metrics + per-segment checksums + ordered-set checksum + output checksum.
- Test asserts artifact JSON includes:
  - `per_segment_input_hashes`
  - `ordered_set_hash`
  - `output_hash`
  - lineage fields proving complete aperture / segment count
- Test reads focused-image pixel payload from `.bin` and verifies nonzero response (`max_abs_pixel > 0`).

5. Verify dropping/reordering one segment fails or changes output deterministically.
- Test runs baseline, then:
  - drop-one-segment config
  - reordered-segment config
- For each perturbation, test passes only if run fails or output checksum differs from baseline.

6. Treat sidecar JSON and SarPy validation JSON as optional evidence only.
- PR9 config/test use `product.crsd` paths only for physics input.
- Docs explicitly state sidecars (`metadata.json`, `pvp.json`, `chunk_index.json`, `provenance.json`, SarPy validation JSON) are optional evidence only.

7. Keep workflow opt-in/local-only and CI independent of real GOTCHA data/SarPy.
- PR9 test is explicitly opt-in (`GRAPHX_SAR_CRSD_ROOT` required) and skips otherwise.
- Existing CTest lane remains disabled and labeled local-only/gated.
- No CI requirement change was introduced.

## Files Changed

- `examples/SAR/config/sar_crsd_gotcha_local_validation.json` (new)
- `examples/SAR/test/test_local_gotcha_validation_lane.cpp` (new)
- `examples/SAR/test/CMakeLists.txt`
- `docs/sar/gotcha_large_scene_data_description.md`
- `docs/CONSOLIDATED_OPERATIONS.md`
- `scripts/convert_gotcha_subdata_to_crsd.sh`

## Files Deleted

- None

## Tests Added

- `LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet`

## Tests Removed

- None

## Build/Test Commands Run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet:RealGotchaFullApertureValidationTest.SkipsCleanlyWhenDatasetNotSet'
```

## Results

- Build: PASS
- `LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet`: SKIPPED by default when `GRAPHX_SAR_CRSD_ROOT` is unset (expected local-only opt-in behavior)
- `RealGotchaFullApertureValidationTest.SkipsCleanlyWhenDatasetNotSet`: PASS

## Constraint Check

- No dataset download logic added.
- No GOTCHA data / generated outputs checked in.
- No MATLAB dependency added.
- No CI requirement changes introduced.
- Workflow remains local-only and opt-in.

## Remaining Follow-Up Work

- PR9 implementation complete in scope.
- Next step is PR9 verifier report: `plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR9.md`.
