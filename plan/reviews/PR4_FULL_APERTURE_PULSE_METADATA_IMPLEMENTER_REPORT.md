# PR4 Implementation Report: Update Normalized Product For Full-Aperture Pulse Metadata

Date: 2026-06-14
Role: IMPLEMENTER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR4, Update Normalized Product For Full-Aperture Pulse Metadata
Status: COMPLETE

## Summary

Implemented PR4 with minimal, scoped model and validator updates for full-aperture pulse/file metadata:

- Minimally extended normalized product metadata to carry optional pulse-file provenance and expected pulse count.
- Extended `SarProductValidator` with new blocking checks for:
  - pulse-count consistency,
  - shape consistency (retained existing behavior),
  - frequency metadata consistency,
  - geometry completeness.
- Added informational warnings for expected per-pulse platform variation (non-blocking).
- Added focused model/validator tests for multi-file full-aperture scenarios.

No reader behavior changes were made. No metadata mapper work was added. No report schema or real-data workflow changes were added.

## Scope Completed

### Model updates (minimal)
File: `examples/SAR/include/sar/io/NormalizedSarProduct.hpp`

Added optional fields only:

- `PerVectorParameters`:
  - `source_file_index` (optional)
  - `source_pulse_index` (optional)
- `CollectionMetadata`:
  - `expected_pulse_count` (optional)

These fields are additive and backward-compatible for existing code paths.

### Validator updates
File: `examples/SAR/include/sar/io/SarProductValidator.hpp`

Extended `SarValidationResult` with warnings:
- `warnings` vector
- `has_warnings()` helper
- `ok()` remains based on blocking `errors` only

Added/extended validations:

1. **Pulse-count consistency (blocking)**
- `pulse_count_mismatch` when `collection.expected_pulse_count` differs from shape pulse count.
- `pulse_count_inconsistent` when pulse count is smaller than `collection.source_files.size()`.

2. **Shape consistency (blocking)**
- Existing shape checks retained (`shape_mismatch`) for channel pulse count and pulse sample count.

3. **Frequency metadata consistency (blocking)**
- `frequency_axis_not_strictly_increasing` when channel axis is non-monotonic.
- `frequency_metadata_mismatch` when channel carrier/bandwidth/sample-rate differs from first channel.
- `frequency_axis_mismatch` when channel axis differs from first channel.

4. **Pulse-file metadata consistency (blocking)**
- `pulse_file_metadata_incomplete` when only one of `source_file_index`/`source_pulse_index` is present.
- `pulse_file_index_out_of_bounds` when file index exceeds `collection.source_files` bounds.
- `pulse_file_sequence_mismatch` when `(source_file_index, source_pulse_index)` is not strictly increasing in vector order.

5. **Geometry completeness (blocking)**
- `geometry_incomplete` when per-pulse `platform.position_m` is all zeros.

6. **Expected platform variation (informational warning)**
- `platform_state_varies_per_pulse` warning emitted when platform state changes across pulses.
- This is non-blocking by design.

## Tests Added

### Model test
File: `examples/SAR/test/test_normalized_sar_product.cpp`

Added:
- `NormalizedSarProductTest.SupportsFullAperturePulseFileMetadataFields`

Validates representation of multi-file pulse metadata and expected pulse count.

### Validator tests
File: `examples/SAR/test/test_sar_product_validator.cpp`

Added:
- `SarProductValidatorTest.ReportsPulseCountConsistencyErrors`
- `SarProductValidatorTest.ReportsFrequencyMetadataConsistencyErrors`
- `SarProductValidatorTest.ReportsPulseFileMetadataCompletenessAndOrderingErrors`
- `SarProductValidatorTest.ReportsGeometryCompletenessErrors`
- `SarProductValidatorTest.EmitsInformationalWarningForPerPulsePlatformVariation`

Updated expectation in:
- `SarProductValidatorTest.ReportsNaNAndInfInMetadataAndSamples`
  - now includes `frequency_metadata_mismatch` as expected due expanded consistency checks.

## Files Changed

- `examples/SAR/include/sar/io/NormalizedSarProduct.hpp`
- `examples/SAR/include/sar/io/SarProductValidator.hpp`
- `examples/SAR/test/test_normalized_sar_product.cpp`
- `examples/SAR/test/test_sar_product_validator.cpp`

## Files Deleted

- None

## Tests Added

- 6 total

## Tests Removed

- None

## Build/Test Commands And Results

### Rebuild
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX/build
ninja test_sar_example_unit
```
Result:
- PASS

### Focused PR4 tests
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX
build/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='SarProductValidatorTest.*:NormalizedSarProductTest.SupportsFullAperturePulseFileMetadataFields'
```
Result:
- PASS
- `12 tests from 2 test suites ran`
- `12 passed`

### Full SAR unit binary
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX
build/examples/SAR/test/test_sar_example_unit
```
Result:
- PASS
- `246 tests from 53 test suites ran`
- `244 passed`
- `2 skipped`
- `0 failed`

## Constraint Compliance

Confirmed:
- No GOTCHA reader behavior changes.
- No graphx-sar-normalized metadata mapping work added.
- No report schema changes added.
- No real-data tests added.
- No MATLAB or new external dependencies added.

## Remaining Follow-Up Work

- None for PR4 scope.
- PR5 can proceed independently.
