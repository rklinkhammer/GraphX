# SAR GOTCHA Full-Aperture Verifier Report: PR3

Date: 2026-06-14
Role: VERIFIER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR3, Add Multi-File Aperture Ordering and Validation

## Verdict

PR3 verification: **PASS with one coverage note**

The implementation satisfies the required ordering, manifest override,
diagnostic, CLI preflight, and scope boundaries. The tests cover valid and
invalid synthetic multi-file apertures. One narrow coverage note remains: the
active unit tests verify contiguous `subData01.mat` through `subData04.mat`,
while the exact real-data ten-file sequence `subData01.mat` through
`subData10.mat` is supported by the generic lexical filename sort and
two-digit aperture parser rather than by an explicit ten-file unit fixture.

## Required Checks

1. Lexical ordering of `subData01.mat` through `subData10.mat` is deterministic:
   **PASS**
   - `GotchaInputOrdering::DiscoverLexical` collects matching `.mat` files and
     sorts by `filename().generic_string()`.
   - GOTCHA aperture filenames use fixed two-digit indices, so lexical order
     places `subData01.mat` through `subData10.mat` in numeric aperture order.
   - `ParseGotchaApertureIndex` recognizes `subDataNN.mat` names and
     `ValidateApertureSequence` enforces strict contiguous increments.
   - Test coverage note: the committed valid sequence test covers
     `subData01.mat` through `subData04.mat`, not an explicit ten-file fixture.

2. Manifest ordering can override lexical ordering: **PASS**
   - Manifest mode reads the manifest `files` array in provided order.
   - `GotchaInputOrderingTest.ManifestOrderingUsesManifestOrder` verifies an
     order different from disk/lexical order.

3. Duplicate, missing, out-of-order, or gapped aperture sequence cases produce
   deterministic diagnostics: **PASS**
   - Duplicate manifest entries report `duplicate_manifest_entry`.
   - Duplicate aperture sequence indices report `duplicate_aperture_sequence`.
   - Missing manifest file reports `manifest_not_found`.
   - Missing manifest entry reports `manifest_entry_not_found`.
   - Out-of-order aperture files report `aperture_sequence_out_of_order`.
   - Gapped aperture files report `aperture_sequence_gap`.
   - Empty lexical input reports `empty_input_directory`.

4. CLI applies ordering before GOTCHA read/conversion: **PASS**
   - `graphx_gotcha_to_crsd.cpp` calls `DiscoverInputs(options)` at the start
     of `Run`.
   - Ordering errors return immediately before MAT support checks, field
     preflight, reader construction, normalized validation, chunking, or export.
   - `GraphxGotchaToCrsdCliTest.GotchaApertureOrderingErrorsFailBeforeReaderInCliPath`
     verifies a gapped `subData01.mat`/`subData03.mat` input fails with
     `aperture_sequence_gap`.

5. Synthetic multi-file tests cover valid and invalid apertures: **PASS**
   - Valid lexical aperture order is covered by
     `LexicalOrderingPreservesContiguousGotchaApertureSequence`.
   - Manifest override order is covered by `ManifestOrderingUsesManifestOrder`.
   - Invalid gap, duplicate sequence, out-of-order sequence, duplicate manifest
     entry, missing manifest, missing manifest entry, and empty input cases are
     covered by `GotchaInputOrderingTest`.
   - CLI invalid aperture behavior is covered by
     `GraphxGotchaToCrsdCliTest.GotchaApertureOrderingErrorsFailBeforeReaderInCliPath`.

6. No metadata mapper, report schema expansion, real-data workflow, CRSD writer,
   or MATLAB dependency was added: **PASS**
   - No `GotchaToCrsdMetadataMapper`, aperture validator class, report schema
     expansion fields such as `total_files_read`, `total_pulses_read`,
     `pulses_per_file`, or `subset_mode` were found in the inspected PR3 path.
   - Existing CRSD/lite writer and local GOTCHA workflow code remains separate
     from this PR3 change.
   - No `find_package(MATLAB)` or MATLAB build/runtime/test dependency was
     introduced.

## Commands Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`
  - Result: **PASS**

- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit '--gtest_filter=GotchaInputOrderingTest.*:GraphxGotchaToCrsdCliTest.GotchaApertureOrderingErrorsFailBeforeReaderInCliPath:GraphxGotchaToCrsdCliTest.InvalidInputAndEmptyInputAndMalformedManifestFailDeterministically:GraphxGotchaToCrsdCliTest.GraphxCrsdLiteModeWorksOnTinyFixture'`
  - Result: **PASS**
  - `14 tests from 2 test suites ran`
  - `14 passed`

- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit`
  - Result: **PASS**
  - `240 tests from 53 test suites ran`
  - `237 passed`
  - `3 skipped`

## Skipped Tests

The full SAR unit binary reported three expected environment-gated skips:

- `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`
- `SarpyCrsdValidationHarnessTest.OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable`
- `LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet`

These skips are local GPU, CRSD path, or real-dataset gates and do not indicate
a PR3 regression.

## Coverage Note

The planner's test description mentioned `subData01.mat` through
`subData10.mat`. The implementation supports this exact naming scheme through
fixed-width lexical sort plus `subDataNN.mat` sequence validation, but the
committed direct unit test uses a shorter valid sequence. This is a small test
coverage gap, not an observed behavior failure.

## Notes

- This verifier did not implement code or redesign PR3.
- The only file written by this verifier pass is this report.
