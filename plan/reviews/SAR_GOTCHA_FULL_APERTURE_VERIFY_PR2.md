# SAR GOTCHA Full-Aperture Verifier Report: PR2

Date: 2026-06-14
Role: VERIFIER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR2, Extend GotchaMatReader To Support Full-Pulse Ingestion

## Verdict

PR2 verification: **PASS**

The required full-pulse ingestion behavior is present, covered by focused
synthetic tests, and the full SAR unit binary is green in the configured
`build-ninja/ninja-debug-metal-native` build.

## Required Checks

1. GotchaMatReader reads all Np pulses per file: **PASS**
   - `examples/SAR/include/sar/io/GotchaMatReader.hpp` reads `Np` from each
     sidecar with `ParseOptionalUnsigned(*sidecar, "Np").value_or(1u)`.
   - The reader iterates `pulse_within_file` from `0` to `Np - 1`.
   - Each valid pulse appends one `PulseVector` to `channel.pulses`.

2. Pulse order within a file is deterministic and preserved: **PASS**
   - Pulses are appended in increasing `pulse_within_file` order.
   - `global_pulse_index` is assigned monotonically to
     `pulse.parameters.vector_index`.
   - `GotchaFullPulseIngestionTest.PulseOrderingWithinFileIsPreserved` verifies
     sequential indices.

3. Normalized output has one PulseVector per pulse: **PASS**
   - The reader creates one `PulseVector` for each pulse represented by `Np`.
   - `GotchaFullPulseIngestionTest.SingleFileWithMultiplePulsesProducesSequentialVectors`
     verifies one file with `Np=3` produces three normalized pulses.

4. Total normalized pulse count equals sum(Np) across input files: **PASS**
   - `GotchaFullPulseIngestionTest.TwoFilesWithMultiplePulsesProduceTotalCount`
     verifies `2 + 3 = 5`.
   - `GotchaFullPulseIngestionTest.MultiFileScenarioWithVaryingPulseCounts`
     verifies `2 + 4 + 3 = 9`.
   - `GotchaFullPulseIngestionTest.TotalPulseCountEqualsSum` verifies
     `10 + 15 + 8 + 12 + 5 = 50`.

5. Synthetic multi-pulse tests cover the new behavior: **PASS**
   - `examples/SAR/test/test_gotcha_full_pulse_ingestion.cpp` exists and is
     included by `examples/SAR/test/CMakeLists.txt`.
   - The test suite covers single-file multi-pulse ingestion, multi-file pulse
     totals, pulse ordering, deterministic repeated reads, large `Np`, channel
     metadata, and platform metadata.

6. No aperture concatenation validator, metadata mapper, report schema
   expansion, CRSD writer, or MATLAB dependency was added: **PASS**
   - No new aperture concatenation validator or GOTCHA metadata mapper was
     identified in the PR2 implementation path.
   - No `total_files_read`, `total_pulses_read`, `pulses_per_file`, or
     `subset_mode` report schema expansion was found in the inspected SAR
     source/test changes.
   - Existing CRSD and graphx-crsd-lite support remains separate from this PR2
     change.
   - No MATLAB build, runtime, or test dependency was introduced.

## Commands Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`
  - Result: **PASS**

- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit '--gtest_filter=GotchaFullPulseIngestionTest.*:GraphxGotchaToCrsdCliTest.GraphxCrsdLiteModeWorksOnTinyFixture:GraphxGotchaToCrsdCliTest.UnsupportedMatFailsClearlyAndCrsdModeProducesSarpyOpenableOutput:GraphxCrsdLiteLaneTest.EndToEndTinySyntheticConversionEmitsReportsAndChecksums:GraphxCrsdLiteLaneTest.RepeatedTinySyntheticConversionIsDeterministic'`
  - Result: **PASS**
  - `14 tests from 3 test suites ran`
  - `14 passed`

- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit`
  - Result: **PASS**
  - `235 tests from 53 test suites ran`
  - `232 passed`
  - `3 skipped`

## Skipped Tests

The full SAR unit binary reported three expected environment-gated skips:

- `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`
- `SarpyCrsdValidationHarnessTest.OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable`
- `LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet`

These skips are local dependency or dataset gates and do not indicate a PR2
regression.

## Notes

- The prior conversion-lane fixture failures caused by missing `Np` fields are
  resolved in the current working tree.
- This verifier did not implement code or redesign the PR. The only file written
  by this verifier pass is this report.
