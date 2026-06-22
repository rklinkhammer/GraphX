# SAR CRSD To Focused Image IMPLEMENTER Report - PR3b

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

Corrective PR: PR3b fixing failures from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR3.md

Title: Complete CRSD aperture assembly adapter contract and negative-proof coverage

## Scope Implemented

PR3b closes all six verifier failure categories from the PR3 verifier report without advancing into PR4.

### Verifier Failure 1 - Geometry-field presence and continuity validation

- Added per-vector geometry validation in `ValidateAssemblyConsistency`:
  - `rcv_time_s` must be finite (rejects NaN/Inf at configure time).
  - `platform_position_m` components must all be finite.
  - `platform_velocity_mps` components must all be finite.
  - `carrier_hz` must be finite (NaN/Inf rejected; 0.0 allowed as "unset").
  - `sample_rate_hz` must be finite.
  - `vector.signal.size()` must equal `segment.samples_per_vector` for every vector.
  - `vector_count` and `samples_per_vector` must be nonzero.
- Tests added: `NonfiniteRcvTimeRejectedAtConfigure`, `InfinitePositionRejectedAtConfigure`, `NanCarrierHzRejectedAtConfigure`, `EmptySignalVectorRejectedAtConfigure`, `TruncatedSignalVectorRejectedAtConfigure`, `ZeroVectorCountSegmentRejectedAtConfigure`.

### Verifier Failure 2 - Explicit invariant that total vector count equals sum of segment vectors

- `ValidateAssemblyConsistency` now computes the sum of all segment `vector_count` fields and throws if it does not equal `total_vector_count` in the read result.
- `BuildApertureMessage` additionally counts emitted payload vectors and returns `nullopt` on mismatch (runtime accounting check after assembly).
- Test added: `TotalVectorCountEqualsSumOfSegmentVectorsAndEmittedPayload` (asserts all three match), `AccountingMismatchInReadResultRejectedAtConfigure`.

### Verifier Failure 3 - Channel consistency contract and validation

- Added `channel_id` field to `CrsdVectorRecord` and `CrsdSegmentRecord` in `CrsdReader.hpp`.
- Added `channel_id` field to `SarPhaseHistoryVector` and `SarPhaseHistorySegment` in `SarPhaseHistoryModel.hpp`.
- `ValidateAssemblyConsistency` now enforces that all segments have the same `channel_id` as the first segment; mismatch throws `inconsistent_channel_id:<segment_index>`.
- Adapter propagates `channel_id` from reader into phase-history model at both segment and per-vector level.
- Tests added: `ChannelIdPropagatedCorrectlyThroughAdapterBoundary`, `InconsistentChannelIdAcrossSegmentsRejectedAtConfigure`.

### Verifier Failure 4 - Complete test coverage for metadata/PVP, ownership/layout, vector ordering, token preservation

- Test added: `MetadataPvpMappingPreservesAllFieldsFieldByField` - checks segment_index, channel_id, vector_count, samples_per_vector, carrier_hz, sample_rate_hz, and per-vector vector_index, channel_id, rcv_time_s, platform_position, platform_velocity, samples size, and sample_payload_hash.
- Test added: `OwnershipSampleFormatAndLayoutAreExplicitAndValid` - asserts OwnedHostBuffer, ComplexFloat32Interleaved, rank/shape/stride, and non-zero hash fields.
- Test added: `PerVectorSampleOrderSurvivesAdapterBoundary` - stamps unique values per vector sample position, asserts exact values survive through adapter.
- Test added: `SarAccelControlTokenPreservedThroughAdapterBoundary` - asserts EOS marker, synthetic=false flag, plus stream_id, backend_id, payload_byte_count identity fields all survive adapter output.

### Verifier Failure 5 - Negative tests for payload drop, sidecar-as-physics misuse, and per-segment final-image

- Test added: `SidecarRoutingFieldsNotUsedAsPhysicsInputWhenCrossCheckDisabled` - tokens with garbage sidecar pulse_range/payload fields are accepted without errors; assembly still completes successfully from typed CRSD payload, proving physics comes from the typed reader result, not sidecar routing.
- Test added: `NoOutputProducedOnDataTokensOnlyOnEos` - every data token returns `nullopt`; only the first EOS produces a message; repeated EOS are no-ops. Proves per-segment-finalization semantics are forbidden.
- Tests `EmptySignalVectorRejectedAtConfigure`, `TruncatedSignalVectorRejectedAtConfigure`, `ZeroVectorCountSegmentRejectedAtConfigure` prove dropped/truncated payloads are rejected before any data is consumed.

### Verifier Failure 6 - Split/merge partition metadata contract for PR4

- Added `SarAperturePartition` and `SarAperturePartitionScheme` structs to `SarPhaseHistoryModel.hpp`:
  - `SarAperturePartition`: partition_id, partition_count, global_vector_start, vector_count, ordering_key, input_boundary_hash, output_boundary_hash.
  - `SarAperturePartitionScheme`: partitions vector, expected_partition_count, merge_ordering_key.
- `SarPhaseHistoryApertureFrame` carries a `partition_scheme` field.
- `BuildApertureMessage` populates the scheme with one partition per segment, covering its vector range, keyed by segment index for deterministic merge ordering in PR4.
- Test added: `PartitionSchemeIsPopulatedWithCorrectContractForPR4` - asserts expected_partition_count, non-zero merge_ordering_key, no overlapping/gapped ranges (vector ranges are contiguous), non-zero input boundary hashes, and unique ordering keys.

## Files Changed

- examples/SAR/include/sar/io/CrsdReader.hpp
- examples/SAR/include/sar/SarPhaseHistoryModel.hpp
- examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp
- examples/SAR/test/test_crsd_aperture_assembly_adapter_node.cpp

## Files Deleted

- None

## Tests Added (17 new, total 23 in suite)

- MetadataPvpMappingPreservesAllFieldsFieldByField
- OwnershipSampleFormatAndLayoutAreExplicitAndValid
- TotalVectorCountEqualsSumOfSegmentVectorsAndEmittedPayload
- ChannelIdPropagatedCorrectlyThroughAdapterBoundary
- InconsistentChannelIdAcrossSegmentsRejectedAtConfigure
- SarAccelControlTokenPreservedThroughAdapterBoundary
- PerVectorSampleOrderSurvivesAdapterBoundary
- PartitionSchemeIsPopulatedWithCorrectContractForPR4
- NonfiniteRcvTimeRejectedAtConfigure
- InfinitePositionRejectedAtConfigure
- NanCarrierHzRejectedAtConfigure
- EmptySignalVectorRejectedAtConfigure
- TruncatedSignalVectorRejectedAtConfigure
- ZeroVectorCountSegmentRejectedAtConfigure
- SidecarRoutingFieldsNotUsedAsPhysicsInputWhenCrossCheckDisabled
- NoOutputProducedOnDataTokensOnlyOnEos
- AccountingMismatchInReadResultRejectedAtConfigure

## Tests Removed

- None

## Out-of-Scope Items (Not Implemented)

- No focused-image transform
- No Metal lane changes
- No sink changes
- No SarPy/reference generation
- No local real-data workflow changes
- No MATLAB dependency changes
- No GraphX runtime contract changes

## Build/Test Commands

- Build:
  - cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit
- Focused validation:
  - ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdApertureAssemblyAdapterNodeTest.*'
- Regression (PR2b suites):
  - ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdReaderTest.*:OrderedCrsdSetInputSourceNodeTest.*'

Result:

- CrsdApertureAssemblyAdapterNodeTest: 23 passed
- CrsdReaderTest: 4 passed
- OrderedCrsdSetInputSourceNodeTest: 4 passed, 1 skipped (gated local smoke)

## Remaining Follow-Up Work

- PR3b verifier pass/report.
- PR4 implementation: CrsdFocusedImageTransformNode consuming SarPhaseHistoryControlMessage via partition scheme.
