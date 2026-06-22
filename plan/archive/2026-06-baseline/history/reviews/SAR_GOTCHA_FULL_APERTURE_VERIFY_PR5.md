> ARCHIVAL STATUS (2026-06-14): This document is kept for historical traceability. It may reference deprecated GraphX SAR conversion lanes, flags, or scripts. Use the active CRSD-only workflow in plan/prompt examples/doc.md and scripts/convert_gotcha_subdata_to_crsd.sh.

# SAR GOTCHA Full-Aperture Verifier Report: PR5

Date: 2026-06-14
Role: VERIFIER
Planner: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`
PR: PR5, Map GOTCHA Metadata To CRSD/Lite Fields

## Verdict

Required checks: **PASS**

Notes:
- Post-rename behavior was verified against `graphx-sar-normalized` (the renamed non-standard intermediate format) per `plan/reviews/SAR_RENAME_CRSD_LITE_IMPLEMENTER_REPORT.md`.
- PR5 requirements referring to "lite" are satisfied in the renamed intermediate metadata path.

## Required Checks

1. carrier_hz derivation formula: **PASS**
   - Implemented in `examples/SAR/include/sar/io/GotchaToCrsdMetadataMapper.hpp`:
     - `carrier_hz = minF + ((K - 1) * deltaF) / 2`
   - Verified by unit test:
     - `examples/SAR/test/test_gotcha_to_crsd_metadata_mapper.cpp`
     - `MapsFrequencyAxisCarrierBandwidthAndSampleCount`

2. bandwidth_hz deterministic derivation from K and deltaF: **PASS**
   - Implemented in mapper:
     - `bandwidth_hz = K * deltaF`
   - Verified by unit test:
     - `examples/SAR/test/test_gotcha_to_crsd_metadata_mapper.cpp`
     - expected value `8.0e6` for `K=4`, `deltaF=2.0e6`

3. frequency_axis contains K sample frequencies: **PASS**
   - Implemented in mapper as `minF + i * deltaF` for `i=0..K-1`
   - Verified by unit test:
     - `examples/SAR/test/test_gotcha_to_crsd_metadata_mapper.cpp`
     - explicit checks for all 4 entries and axis size

4. AntX/AntY/AntZ and R0 preserved in lite metadata: **PASS**
   - Mapper converts:
     - `AntX/AntY/AntZ -> antenna_xyz_m`
     - `R0 -> reference_range_m`
   - Reader applies mapping to pulse metadata:
     - `examples/SAR/include/sar/io/GotchaMatReader.hpp`
     - `ApplyToPulse(...)` sets pulse platform position and `reference_range_m`
   - Writer emits into normalized metadata JSON:
     - `examples/SAR/include/sar/io/GraphxSarNormalizedIO.hpp`
     - pulse fields include `antenna_xyz`, `antenna_phase_center_m`, `reference_range_m`
   - End-to-end lane test verifies serialized values:
     - `examples/SAR/test/test_graphx_sar_normalized_lane.cpp`

5. Local Cartesian scene-center frame labeled clearly: **PASS**
   - Mapper sets frame label:
     - `GotchaToCrsdMetadataMapper::kLocalCartesianFrame = "gotcha_local_cartesian"`
   - Applied to collection coordinate frame:
     - `ApplyToProductCollection(...)`
   - Writer emits geometry and pulse frame labels:
     - `geometry.coordinate_frame`
     - `local_geometry_frame`
   - Verified in lane test:
     - `examples/SAR/test/test_graphx_sar_normalized_lane.cpp`

6. Round-trip/unit tests verify metadata preservation: **PASS**
   - Unit mapper tests:
     - `examples/SAR/test/test_gotcha_to_crsd_metadata_mapper.cpp`
   - Writer/reader round-trip test:
     - `examples/SAR/test/test_graphx_sar_normalized_io.cpp`
     - `ReaderRoundTripsNormalizedProductAndPulseOrdering`
   - End-to-end conversion/determinism tests:
     - `examples/SAR/test/test_graphx_sar_normalized_lane.cpp`

7. No real-data workflow, standards CRSD expansion, MATLAB dependency, or new external dependency: **PASS**
   - No new real-data gating/workflow required for PR5 checks.
   - No standards-CRSD expansion detected in PR5 mapping path; mapping targets non-standard normalized package metadata.
   - No MATLAB runtime dependency introduced for mapping path.
   - No new external dependency additions detected in SAR CMake test/build paths.

## Commands Run

- Build SAR test binary:
  - `cd /Users/rklinkhammer/workspace/GraphX/build && ninja test_sar_example_unit`
  - Result: **PASS**

- Focused PR5 verification tests:
  - `cd /Users/rklinkhammer/workspace/GraphX && build/examples/SAR/test/test_sar_example_unit --gtest_filter='GotchaToCrsdMetadataMapperTest.*:GraphxSarNormalizedLaneTest.*:GraphxSarNormalizedIoTest.ReaderRoundTripsNormalizedProductAndPulseOrdering:GraphxSarNormalizedIoTest.WriterEmitsRequiredFilesAndNonStandardLabels'`
  - Result: **PASS**
  - `7 tests from 3 test suites ran`, `7 passed`

## Conclusion

PR5 verification checks are satisfied. The metadata mapping formulas, geometry/reference-range preservation, local-frame labeling, and round-trip verification are implemented and tested in the renamed intermediate format path.
