# PR3 Implementation Report: Add Multi-File Aperture Ordering And Validation

Date: 2026-06-14
Role: IMPLEMENTER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR3, Add Multi-File Aperture Ordering And Validation
Status: COMPLETE

## Summary

Implemented deterministic multi-file aperture ordering validation in `GotchaInputOrdering` for GOTCHA-style `subDataNN.mat` inputs.

The change preserves existing lexical and manifest modes, keeps manifest order authoritative for opaque filenames, and adds deterministic diagnostics for GOTCHA aperture sequence problems when the filenames provide enough information:
- `duplicate_aperture_sequence`
- `aperture_sequence_out_of_order`
- `aperture_sequence_gap`

The CLI already applies input ordering before reader ingestion through `DiscoverInputs()`, so no production CLI routing change was required. The new validation now fails invalid apertures in that existing pre-reader path.

## Scope Completed

### Implemented
- Added GOTCHA aperture filename parsing for exact `subDataNN.mat` names.
- Added deterministic aperture sequence validation in lexical mode.
- Added deterministic aperture sequence validation in manifest mode.
- Preserved manifest override behavior for non-GOTCHA opaque filenames.
- Ensured invalid lexical ordering clears the ordered file list the same way existing manifest failures do.
- Added CI-safe synthetic tests covering valid and invalid aperture ordering.
- Verified CLI fails before reader ingestion on an invalid lexical aperture.

### Not Implemented
- No normalized product model changes.
- No metadata mapper work.
- No report schema changes.
- No real-data tests.
- No MATLAB or new external dependencies.

## Files Changed

- `examples/SAR/include/sar/io/GotchaInputOrdering.hpp`
- `examples/SAR/test/test_gotcha_input_ordering.cpp`
- `examples/SAR/test/test_graphx_gotcha_to_crsd_cli.cpp`

## Files Deleted

- None

## Tests Added

Added 5 focused tests:

In `examples/SAR/test/test_gotcha_input_ordering.cpp`:
1. `LexicalOrderingPreservesContiguousGotchaApertureSequence`
2. `LexicalOrderingReportsGapInGotchaApertureSequence`
3. `ManifestOrderingReportsOutOfOrderGotchaApertureSequence`
4. `ManifestOrderingReportsDuplicateGotchaApertureSequence`

In `examples/SAR/test/test_graphx_gotcha_to_crsd_cli.cpp`:
5. `GotchaApertureOrderingErrorsFailBeforeReaderInCliPath`

## Tests Removed

- None

## Build/Test Command And Result

### Rebuild
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX/build
ninja test_sar_example_unit
```
Result:
- PASS

### Focused ordering and CLI validation
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX
build/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GotchaInputOrderingTest.*:GraphxGotchaToCrsdCliTest.GotchaApertureOrderingErrorsFailBeforeReaderInCliPath'
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
- `240 tests from 53 test suites ran`
- `238 passed`
- `2 skipped`
- `0 failed`

## Exact Behavior Added

### GOTCHA aperture parsing
`GotchaInputOrdering` now recognizes filenames of the form:
- `subData01.mat`
- `subData02.mat`
- ...
- `subData10.mat`

Validation is only applied when **all** ordered files match the GOTCHA aperture filename pattern. Opaque or mixed filename sets keep existing ordering behavior unchanged. This avoids false positives when the model does not provide enough sequence information.

### Deterministic diagnostics
When all files match the GOTCHA aperture pattern, ordering validation now reports:

- `duplicate_aperture_sequence`
  - same aperture index appears more than once
  - example: `a/subData01.mat`, `b/subData01.mat`

- `aperture_sequence_out_of_order`
  - sequence is not strictly increasing in manifest order
  - example: `subData02.mat`, `subData01.mat`, `subData03.mat`

- `aperture_sequence_gap`
  - a contiguous sequence is broken
  - example: `subData01.mat`, `subData03.mat`

### CLI path behavior
No new CLI wiring was added because `graphx_gotcha_to_crsd` already calls `DiscoverInputs()` before format validation, field validation, and reader construction.

The new ordering validation now triggers in that existing pre-reader path, which the new CLI test verifies.

## Remaining Follow-Up Work

- None for PR3 scope.
- PR4 can proceed independently.

## Scope Confirmation

The following constraints were honored:
- Did not change the normalized product model except not needed here.
- Did not add metadata mapper work.
- Did not add report schema work.
- Did not add real-data tests.
- Did not add MATLAB or new external dependencies.
