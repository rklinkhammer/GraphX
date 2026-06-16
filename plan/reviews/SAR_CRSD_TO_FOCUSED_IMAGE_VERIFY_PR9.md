# SAR CRSD To Focused Image VERIFIER Report - PR9

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: PR9 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Local-only GOTCHA-derived CRSD validation workflow

## Verification Result

PASS

## Findings (ordered by severity)

1. Local real-data workflow is explicitly opt-in and local-only.
- `LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet` requires `GRAPHX_SAR_CRSD_ROOT`; otherwise it skips with explicit opt-in message.
- CTest lane `sar_real_gotcha_local_validation` is disabled and labeled `local-only;gated`.

2. Workflow enforces ordered CRSD layout for one aperture set.
- Config `examples/SAR/config/sar_crsd_gotcha_local_validation.json` enumerates ordered `subData01..subData10 .../product.crsd` paths.
- Test builds this same ordered list and uses it as a single `crsd_paths` set into `OrderedCrsdSetInputSourceNode`.

3. Workflow validates single focused-image artifact set and checksum/metric evidence.
- Test asserts exactly one `.bin`, one `.json`, and one `.pgm` output artifact set.
- Test asserts `per_segment_input_hashes`, `ordered_set_hash`, `output_hash`, lineage completeness/segment count.
- Test reads focused-image pixel payload from artifact and verifies nonzero response (`max_abs_pixel > 0`).

4. Drop/reorder perturbation behavior is covered.
- Test verifies dropping one segment or reordering segments must either fail or produce a different output checksum from baseline.

5. Sidecar boundary policy is documented correctly.
- Docs state `product.crsd` is authoritative.
- `metadata.json`, `pvp.json`, `chunk_index.json`, `provenance.json`, and SarPy validation JSON are explicitly documented as optional evidence only.

6. No prohibited additions observed in PR9 scope.
- No dataset download behavior added.
- No checked-in GOTCHA/generated output artifacts introduced.
- No MATLAB dependency introduced.
- No CI requirement changes observed.

## Required Checks

1. Real-data workflow is explicitly local-only and opt-in.
- PASS

2. CI remains independent of real GOTCHA data and SarPy.
- PASS
- Evidence: local lane disabled/gated and skip-safe; CI does not require env/data for this lane.

3. Workflow accepts generated CRSD layout with subData01 through subData10 product.crsd files as one ordered aperture set.
- PASS

4. Workflow emits one final focused-image artifact set, not one image per CRSD segment.
- PASS

5. Workflow records per-segment input checksums, ordered-set checksum, output checksum, and nonzero image metrics.
- PASS

6. Dropping or reordering one segment fails or changes output deterministically.
- PASS

7. Sidecar JSON and SarPy validation JSON are optional evidence only, not authoritative signal/PVP inputs.
- PASS

8. No downloads, checked-in GOTCHA data, checked-in generated outputs, MATLAB dependency, or CI real-data requirement was added.
- PASS

## Commands/Evidence Used

```bash
git status --short

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet'

GRAPHX_SAR_CRSD_ROOT=/tmp/nonexistent_graphx_pr9 \
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet'

rg -n "download|wget|curl|MATLAB|GRAPHX_SAR_CRSD_ROOT|local-only|opt-in|ordered_set_checksum|ordered_segments" \
  scripts/convert_gotcha_subdata_to_crsd.sh \
  docs/sar/gotcha_large_scene_data_description.md \
  docs/CONSOLIDATED_OPERATIONS.md \
  examples/SAR/test/test_local_gotcha_validation_lane.cpp \
  examples/SAR/config/sar_crsd_gotcha_local_validation.json \
  examples/SAR/test/CMakeLists.txt
```

## Verifier Conclusion

PR9 satisfies the required checks. The implementation introduces an explicit local-only, opt-in real-data validation lane for ordered CRSD set processing, validates single focused-image artifact and checksum/metric evidence, enforces deterministic drop/reorder behavior, keeps sidecar files non-authoritative, and preserves CI independence from real GOTCHA data and SarPy.
