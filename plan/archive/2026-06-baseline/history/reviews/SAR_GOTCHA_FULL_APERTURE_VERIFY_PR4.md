> ARCHIVAL STATUS (2026-06-14): This document is kept for historical traceability. It may reference deprecated GraphX SAR conversion lanes, flags, or scripts. Use the active CRSD-only workflow in plan/prompt examples/doc.md and scripts/convert_gotcha_subdata_to_crsd.sh.

# PR4 Verification Report: Update Normalized Product For Full-Aperture Pulse Metadata

Date: 2026-06-14
Role: VERIFIER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR4, Update Normalized Product For Full-Aperture Pulse Metadata
Status: PASS

## Executive Verdict

PR4 satisfies the required verification checks.

The normalized SAR product can now represent pulse provenance across multiple source files, the validator has blocking checks for pulse count, shape, frequency metadata, and geometry completeness, and per-pulse antenna/platform variation is reported as a non-blocking warning.

One non-blocking implementation detail differs from the planner's suggested test-file name: focused coverage was added to existing behavior-oriented tests instead of a new `test_normalized_product_multi_file_validation.cpp`. The required multi-file validation behavior is covered and passing.

## Required Checks

1. **NormalizedSarProduct can represent sum(Np) pulses across multiple files: PASS**
   - `PerVectorParameters` carries optional `source_file_index` and `source_pulse_index`.
   - `CollectionMetadata` carries `source_files` and optional `expected_pulse_count`.
   - `NormalizedSarProduct::Shape()` reports pulse count from channel pulse vectors.
   - Test `NormalizedSarProductTest.SupportsFullAperturePulseFileMetadataFields` models 5 pulses from 2 source files and verifies the expected pulse count and final source pulse coordinates.

2. **Validator checks pulse count, shape consistency, frequency metadata consistency, and geometry completeness: PASS**
   - `ValidatePulseCountConsistency` emits `pulse_count_mismatch` and `pulse_count_inconsistent`.
   - `ValidateShapeConsistency` emits `shape_mismatch` for channel pulse count and sample-count mismatches.
   - `ValidateFrequencyMetadataConsistency` checks increasing frequency axes and cross-channel carrier/bandwidth/sample-rate/frequency-axis consistency.
   - `ValidateGeometryCompleteness` emits `geometry_incomplete` for missing per-pulse platform positions.

3. **Validator diagnostics distinguish blocking errors from informational warnings: PASS**
   - `SarValidationResult` has separate `errors` and `warnings` vectors.
   - `ok()` returns true when there are no blocking errors, independent of warnings.
   - `has_warnings()` exposes informational diagnostics separately.

4. **Antenna/platform differences are not incorrectly blocked when represented per-pulse: PASS**
   - `ValidatePlatformVariationInfo` emits `platform_state_varies_per_pulse` through `AddWarning`, not `AddError`.
   - Test `SarProductValidatorTest.EmitsInformationalWarningForPerPulsePlatformVariation` verifies that per-pulse platform motion keeps `result.ok()` true while producing one warning.

5. **Focused tests cover multi-file validation: PASS**
   - `NormalizedSarProductTest.SupportsFullAperturePulseFileMetadataFields` covers multi-file pulse provenance representation.
   - `SarProductValidatorTest.ReportsPulseCountConsistencyErrors` covers expected pulse count and source file count validation.
   - `SarProductValidatorTest.ReportsPulseFileMetadataCompletenessAndOrderingErrors` covers deterministic source-file/source-pulse ordering validation.
   - Related validator tests cover frequency metadata consistency, geometry completeness, shape consistency, and warning/error separation.

6. **No lite metadata mapper, real-data workflow, CRSD writer, or MATLAB dependency was added: PASS**
   - PR4 implementation files are limited to the normalized model, validator, and their tests.
   - Searches of the PR4 implementation files found no CRSD writer, metadata mapper, graphx-sar-normalized writer/reader, SarPy, or MATLAB dependency additions.
   - Existing repository CRSD and local-only workflow code remains outside this PR4 change.

## Files Reviewed

- `examples/SAR/include/sar/io/NormalizedSarProduct.hpp`
- `examples/SAR/include/sar/io/SarProductValidator.hpp`
- `examples/SAR/test/test_normalized_sar_product.cpp`
- `examples/SAR/test/test_sar_product_validator.cpp`
- `plan/reviews/PR4_FULL_APERTURE_PULSE_METADATA_IMPLEMENTER_REPORT.md`

## Scope Notes

The working tree also contains a modified planning prompt file:

- `plan/agents/SAR_GOTCHA_FULL_APERTURE_PR_AGENTS.md`

That diff is prompt/report text only and does not alter active code, tests, tools, CMake, runtime behavior, CRSD writing, real-data workflows, or MATLAB dependencies. It is outside the PR4 runtime implementation surface.

## Verification Commands

Build:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit
```

Result: PASS.

Focused PR4 tests:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=NormalizedSarProductTest.*:SarProductValidatorTest.*'
```

Result: PASS. `17 tests from 2 test suites ran`; `17 passed`.

Full SAR unit binary:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit
```

Result: PASS. `246 tests from 53 test suites ran`; `243 passed`; `3 skipped`; `0 failed`.

Expected skips:

- `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`
- `SarpyCrsdValidationHarnessTest.OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable`
- `LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet`

The full run emitted Matplotlib cache warnings because `/Users/rklinkhammer/.matplotlib` is not writable in this environment; the affected tests passed.

## Conclusion

PR4 is verified complete. It adds the full-aperture pulse metadata hooks and validator guardrails needed at this stage without introducing mapper, real-data, CRSD writer, or MATLAB dependency work.
