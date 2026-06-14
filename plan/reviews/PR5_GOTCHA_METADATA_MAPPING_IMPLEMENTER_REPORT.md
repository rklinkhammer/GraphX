# PR5 Implementation Report: Map GOTCHA Metadata To CRSD/Lite Fields

Date: 2026-06-14
Role: IMPLEMENTER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR5, Map GOTCHA Metadata To CRSD/Lite Fields
Status: COMPLETE

## Summary

Implemented PR5 against the current post-rename intermediate lane:

- Planner-era `graphx-crsd-lite` references map to the active `graphx-sar-normalized` format.
- Added a focused GOTCHA raw-field metadata mapper in the SAR IO area.
- Mapped `K`, `deltaF`, and `minF` into waveform frequency metadata.
- Mapped `AntX`, `AntY`, `AntZ`, and `R0` into per-pulse local Cartesian geometry metadata.
- Extended `graphx-sar-normalized` metadata output and reader round-trip handling for mapped antenna/reference-range metadata.
- Added focused unit and end-to-end tests proving mapped metadata appears in generated metadata JSON.

No standards-compliant CRSD metadata expansion, real-data test, MATLAB dependency, external dependency, or local-only workflow change was added.

## Files Changed

- `examples/SAR/include/sar/io/GotchaToCrsdMetadataMapper.hpp`
  - New focused mapper.
  - Computes:
    - `frequency_axis_hz[i] = minF + i * deltaF`
    - `carrier_hz = minF + (K - 1) * deltaF / 2`
    - `bandwidth_hz = K * deltaF`
    - `sample_count = K`
    - `antenna_xyz_m = [AntX, AntY, AntZ]`
    - `reference_range_m = R0`
    - `coordinate_frame = gotcha_local_cartesian`

- `examples/SAR/include/sar/io/NormalizedSarProduct.hpp`
  - Added optional `PerVectorParameters::reference_range_m`.

- `examples/SAR/include/sar/io/GotchaMatReader.hpp`
  - Applies raw GOTCHA metadata mapping when the required raw fields are present.
  - Falls back to existing normalized sidecar fields when raw GOTCHA mapping fields are absent.
  - Sets per-pulse `source_file_index`, `source_pulse_index`, and collection `expected_pulse_count`.

- `examples/SAR/include/sar/io/GraphxSarNormalizedIO.hpp`
  - Writes local Cartesian geometry metadata to `metadata.json`.
  - Preserves:
    - `geometry.coordinate_frame`
    - `geometry.antenna_position_frame`
    - `pulses[].local_geometry_frame`
    - `pulses[].antenna_phase_center_m`
    - `pulses[].antenna_xyz`
    - `pulses[].reference_range_m`
  - Reads `reference_range_m` back into the normalized product.
  - Writes/reads optional `collection.expected_pulse_count`.

- `examples/SAR/include/sar/io/SarProductValidator.hpp`
  - Validates optional `reference_range_m` is finite when present.

- `examples/SAR/test/CMakeLists.txt`
  - Added the new mapper unit test source.

- `examples/SAR/test/test_gotcha_to_crsd_metadata_mapper.cpp`
  - Added mapper unit tests for frequency metadata, antenna geometry, reference range, and missing-field behavior.

- `examples/SAR/test/test_gotcha_mat_reader.cpp`
  - Added raw GOTCHA sidecar mapping test.

- `examples/SAR/test/test_graphx_sar_normalized_io.cpp`
  - Added metadata JSON assertions and reader round-trip checks for `reference_range_m`.

- `examples/SAR/test/test_graphx_sar_normalized_lane.cpp`
  - Added end-to-end CLI-generated metadata assertions for mapped waveform and local geometry fields.

- `examples/SAR/test/test_gotcha_full_pulse_ingestion.cpp`
  - Adjusted legacy normalized-sidecar assertions so they continue to test normalized fallback behavior when raw GOTCHA mapping fields are absent.

## Files Deleted

- None.

## Tests Added

- `GotchaToCrsdMetadataMapperTest.MapsFrequencyAxisCarrierBandwidthAndSampleCount`
- `GotchaToCrsdMetadataMapperTest.MapsAntennaPhaseCenterReferenceRangeAndLocalFrame`
- `GotchaToCrsdMetadataMapperTest.MissingRawGotchaFieldsDoesNotProduceMapping`
- `GotchaMatReaderTest.RawGotchaMetadataMapsToWaveformAndLocalGeometry`

Additional assertions were added to:

- `GraphxSarNormalizedIoTest.WriterEmitsRequiredFilesAndNonStandardLabels`
- `GraphxSarNormalizedIoTest.ReaderRoundTripsNormalizedProductAndPulseOrdering`
- `GraphxSarNormalizedLaneTest.EndToEndTinySyntheticConversionEmitsReportsAndChecksums`

## Tests Removed

- None.

## Build/Test Commands And Results

Build:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit
```

Result: PASS.

Focused PR5 tests:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  '--gtest_filter=GotchaToCrsdMetadataMapperTest.*:GotchaMatReaderTest.*:GraphxSarNormalizedIoTest.*:GraphxSarNormalizedLaneTest.*:GotchaFullPulseIngestionTest.*:SarProductValidatorTest.*:GraphxGotchaToCrsdCliTest.GraphxSarNormalizedModeWorksOnTinyFixture'
```

Result: PASS. `35 tests from 7 test suites ran`; `35 passed`.

Full SAR unit binary:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit
```

Result: PASS. `250 tests from 54 test suites ran`; `247 passed`; `3 skipped`; `0 failed`.

Expected skips:

- `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`
- `SarpyCrsdValidationHarnessTest.OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable`
- `LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet`

The full run emitted Matplotlib cache warnings because `/Users/rklinkhammer/.matplotlib` is not writable in this environment; the affected tests passed.

## Remaining Follow-Up Work

- PR6 can add broader synthetic multi-file/multi-pulse fixtures independently.
- Standards-compliant CRSD metadata remains outside PR5 and was not expanded here.
