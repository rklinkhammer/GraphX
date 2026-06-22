# PR7 Implementation Report: Update Conversion Report For Full-Aperture Pulse Accounting

Date: 2026-06-14
Role: IMPLEMENTER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR7, Update Conversion Report For Full-Aperture Pulse Accounting
Status: COMPLETE

## Summary

Implemented PR7 with minimal, scoped extensions to the `ConversionReportBuildInput` struct, `BuildConversionReportJson` serializer, and a new `ComputePulsesPerFile` static helper. Both output paths (the `GraphxSarNormalizedWriter` and the CLI `graphx_gotcha_to_crsd`) now emit a structured `aperture_accounting` section in every `conversion_report.json`.

No reader behavior was changed. No metadata mapper work was added. No real-data tests were added. No standards CRSD writer work was added.

## Scope Completed

### ConversionReportBuildInput extension
File: `examples/SAR/include/sar/io/SarIoUtilities.hpp`

Added new `SarPulseFileCount` struct:
```cpp
struct SarPulseFileCount {
    std::string filename{};
    std::size_t pulse_count{0};
};
```

Added to `ConversionReportBuildInput`:
- `total_files_read` (size_t)
- `total_pulses_read` (size_t)
- `pulses_per_file` (vector<SarPulseFileCount>)
- `aperture_mode` (string, default "full_aperture")
- `pulse_selection_method` (string, empty by default; emitted only when set)

### BuildConversionReportJson output
`conversion_report.json` now contains a new `aperture_accounting` object:
```json
{
  "aperture_accounting": {
    "total_files_read": 2,
    "total_pulses_read": 20,
    "aperture_mode": "full_aperture",
    "pulses_per_file": [
      {"filename": "subData01.mat", "pulse_count": 10},
      {"filename": "subData02.mat", "pulse_count": 10}
    ]
  }
}
```
When `aperture_mode == "subset"` and `pulse_selection_method` is set, it is emitted:
```json
{
  "aperture_accounting": {
    "aperture_mode": "subset",
    "pulse_selection_method": "single_index",
    ...
  }
}
```

### ComputePulsesPerFile static helper
Added to `SarIoUtilities`:
- Scans per-pulse `source_file_index` (PR4 provenance field) to count pulses per file.
- Falls back to attributing all pulses to the single source file when a single-file product lacks per-pulse provenance.
- Falls back to emitting files with `pulse_count=0` when multi-file product lacks provenance (non-blocking; the field still appears for observability).

### GraphxSarNormalizedWriter population
File: `examples/SAR/include/sar/io/GraphxSarNormalizedIO.hpp`

Before building the report, calls `ComputePulsesPerFile(product)` and populates:
- `total_files_read = product.collection.source_files.size()`
- `total_pulses_read = shape.pulse_count`
- `pulses_per_file = ComputePulsesPerFile(product)`
- `aperture_mode = "full_aperture"`

### CLI population
File: `examples/SAR/src/graphx_gotcha_to_crsd.cpp`

Root conversion report call now populates the same set of fields using the already-available `read.product`.

## Files Changed

- `examples/SAR/include/sar/io/SarIoUtilities.hpp`
- `examples/SAR/include/sar/io/GraphxSarNormalizedIO.hpp`
- `examples/SAR/src/graphx_gotcha_to_crsd.cpp`
- `examples/SAR/test/test_conversion_report_pulse_accounting.cpp` (new)
- `examples/SAR/test/CMakeLists.txt`

## Files Deleted

- None

## Tests Added

9 (in `test_conversion_report_pulse_accounting.cpp`):

- `ConversionReportSchemaTest.EmitsApertureAccountingSection`
- `ConversionReportSchemaTest.SubsetModeEmitsPulseSelectionMethod`
- `ComputePulsesPerFileTest.TwoFileProductReturnsCorrectCounts`
- `ComputePulsesPerFileTest.TenFileProductReturnsCorrectCounts`
- `ComputePulsesPerFileTest.SingleFileNoProvenanceFallback`
- `ConversionReportPulseAccountingTest.NormalizedWriterEmitsApertureAccountingForTwoFileProduct`
- `ConversionReportPulseAccountingTest.NormalizedWriterEmitsApertureAccountingForTenFileProduct`
- `ConversionReportPulseAccountingTest.ApertureAccountingIsDeterministicAcrossRepeatedWrites`
- `ConversionReportPulseAccountingTest.FullApertureReadFromFixtureProducesCorrectReportCounts`

## Tests Removed

- None

## Build/Test Commands And Results

### Rebuild
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX
cmake --build build --target test_sar_example_unit -j8
```
Result:
- PASS (no warnings beyond pre-existing duplicate-library linker note)

### Focused PR7 tests
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX
./build/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=ConversionReportSchemaTest.*:ComputePulsesPerFileTest.*:ConversionReportPulseAccountingTest.*'
```
Result:
- PASS
- `9 tests from 3 test suites ran`
- `9 passed`

### Full SAR unit binary
Command:
```bash
cd /Users/rklinkhammer/workspace/GraphX
./build/examples/SAR/test/test_sar_example_unit
```
Result:
- PASS
- `262 tests from 58 test suites ran`
- `260 passed`
- `2 skipped`
- `0 failed`

## Constraint Compliance

Confirmed:
- No reader behavior changes.
- No metadata mapper work beyond existing fields.
- No real-data tests added.
- No standards CRSD writer work added.
- No MATLAB or new external dependency introduced.

## Remaining Follow-Up Work

- None for PR7 scope.
- PR8 can proceed independently.
