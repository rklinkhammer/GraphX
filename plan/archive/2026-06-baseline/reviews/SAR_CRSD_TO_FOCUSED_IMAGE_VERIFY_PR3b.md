# SAR CRSD To Focused Image VERIFIER Report - PR3 + PR3b

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PRs Verified: PR3 + PR3b from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Verdict: **PASS**

All required verifier checks are satisfied by the combined PR3 + PR3b implementation. The PR3 verifier report (plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR3.md) issued FAIL due to six incomplete contract areas; PR3b addressed all six. This report closes the verification gate for the combined state.

## Verification Commands Executed

Build:
- cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

Adapter-focused test run:
- ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdApertureAssemblyAdapterNodeTest.*'

Regression (CRSD reader and source node suites):
- ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdReaderTest.*:OrderedCrsdSetInputSourceNodeTest.*'

Results:
- CrsdApertureAssemblyAdapterNodeTest: 23 passed, 0 failed
- CrsdReaderTest: 4 passed, 0 failed
- OrderedCrsdSetInputSourceNodeTest: 4 passed, 1 skipped (gated local smoke)

## Required Checks Matrix

### 1. CrsdApertureAssemblyAdapterNode emits assembled full-aperture SAR phase-history messages/tokens.
- Status: **PASS**
- Evidence:
  - `Transfer` returns a `SarPhaseHistoryControlMessage` only on `SarFrameMarker::EndOfStream` after all segments are seen, via `BuildApertureMessage` in examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp.
  - `frame.control_marker` is set to `SarPhaseHistoryControlMarker::EndOfStream`.
  - `control.sidecar.synthetic` is cleared to `false`.
  - `AssemblesFullApertureFrameOnEndOfStream` confirms full-aperture output with correct segment count, total vector count, and control marker.

### 2. Required geometry and sampling fields are present and validated.
- Status: **PASS**
- Evidence:
  - `CrsdVectorRecord` carries: `vector_index`, `channel_id`, `rcv_time_s`, `platform_position_m[3]`, `platform_velocity_mps[3]`, `signal`.
  - `CrsdSegmentRecord` carries: `channel_id`, `samples_per_vector`, `carrier_hz`, `sample_rate_hz`.
  - `ValidateAssemblyConsistency` enforces at configure time:
    - `rcv_time_s` must be finite (`NonfiniteRcvTimeRejectedAtConfigure` passes).
    - All three `platform_position_m` components must be finite (`InfinitePositionRejectedAtConfigure` passes).
    - All three `platform_velocity_mps` components must be finite.
    - `carrier_hz` and `sample_rate_hz` must be finite (`NanCarrierHzRejectedAtConfigure` passes).
    - `vec.signal.size()` must equal `segment.samples_per_vector` (`EmptySignalVectorRejectedAtConfigure`, `TruncatedSignalVectorRejectedAtConfigure` pass).
    - `vector_count` and `samples_per_vector` must be nonzero (`ZeroVectorCountSegmentRejectedAtConfigure` passes).
  - All geometry fields survive to `SarPhaseHistoryVector` in the assembled frame, confirmed field-by-field by `MetadataPvpMappingPreservesAllFieldsFieldByField`.

### 3. Total output vector count equals sum of segment vectors.
- Status: **PASS**
- Evidence:
  - `ValidateAssemblyConsistency` computes sum of all `segment.vector_count` fields and throws `ConfigError` if it does not equal `total_vector_count` in the reader result (`AccountingMismatchInReadResultRejectedAtConfigure` passes).
  - `BuildApertureMessage` counts emitted payload vectors post-assembly and returns `nullopt` on mismatch.
  - `TotalVectorCountEqualsSumOfSegmentVectorsAndEmittedPayload` explicitly asserts:
    - `frame.total_vector_count == 9` (3 segments × 3 vectors).
    - Sum of `segment.vector_count` fields == `frame.total_vector_count`.
    - Count of `segment.vectors.size()` entries == `frame.total_vector_count`.

### 4. Sample/channel/frequency consistency is enforced across segments.
- Status: **PASS**
- Evidence:
  - `ValidateAssemblyConsistency` checks all segments against `segments.front()` reference values:
    - `samples_per_vector` must match (`EnforcesSampleAndFrequencyConsistencyAtConfigure` passes).
    - `carrier_hz` must match within 1e-9 (or both zero; finite mismatch throws `inconsistent_carrier_hz`).
    - `sample_rate_hz` must match within 1e-9 (or both zero).
    - `channel_id` must match (`InconsistentChannelIdAcrossSegmentsRejectedAtConfigure` passes).
  - Diagnostic strings are deterministic: `inconsistent_samples_per_vector:<index>`, `inconsistent_carrier_hz:<index>`, `inconsistent_sample_rate_hz:<index>`, `inconsistent_channel_id:<index>`.

### 5. Tests cover segment ordering, full-aperture accounting, metadata/PVP mapping, EOS/control-marker propagation, ownership/layout, checksums, vector/channel/sample ordering, and SarAccelControlToken preservation.
- Status: **PASS**
- Evidence by coverage area:
  - Segment ordering: `AssemblesFullApertureFrameOnEndOfStream`, `DetectsOutOfOrderAndMissingSegmentDiagnostics`, `DetectsDuplicateAndUnexpectedSegments`.
  - Full-aperture accounting: `TotalVectorCountEqualsSumOfSegmentVectorsAndEmittedPayload`, `AccountingMismatchInReadResultRejectedAtConfigure`.
  - Metadata/PVP mapping (field-by-field): `MetadataPvpMappingPreservesAllFieldsFieldByField` — asserts `segment_index`, `channel_id`, `vector_count`, `samples_per_vector`, `carrier_hz`, `sample_rate_hz`, per-vector `vector_index`, `channel_id`, `rcv_time_s`, `platform_position_m[0..2]`, `platform_velocity_mps[0]`, `samples.size()`, `sample_payload_hash`.
  - EOS/control-marker propagation: `AssemblesFullApertureFrameOnEndOfStream` — asserts `control.sidecar.marker == EndOfStream` and `frame.control_marker == EndOfStream`.
  - Ownership/layout/checksums: `OwnershipSampleFormatAndLayoutAreExplicitAndValid` — asserts `OwnedHostBuffer`, `ComplexFloat32Interleaved`, `layout.rank == 2`, `layout.shape[0] == total_vector_count`, `layout.shape[1] == samples_per_vector * 2`, `layout.stride[0] == shape[1]`, `layout.stride[1] == 1`, non-zero `ordered_set_payload_hash`, `split_boundary_input_hash == ordered_set_payload_hash`, non-zero `split_boundary_output_hash`.
  - Vector/channel/sample ordering: `PerVectorSampleOrderSurvivesAdapterBoundary` — unique per-position sample values stamped in input and verified exactly in output. `ChannelIdPropagatedCorrectlyThroughAdapterBoundary` verifies channel_id at both segment and per-vector level.
  - SarAccelControlToken preservation: `SarAccelControlTokenPreservedThroughAdapterBoundary` — asserts `marker == EndOfStream`, `synthetic == false`, `stream_id`, `backend_id`, `payload_byte_count` all survive from input EOS token.

### 6. Tests fail if payload data is dropped, sidecars are used as physics inputs, or each segment is treated as a separate final image.
- Status: **PASS**
- Evidence:
  - Payload drop: `EmptySignalVectorRejectedAtConfigure` throws on empty signal; `TruncatedSignalVectorRejectedAtConfigure` throws on signal.size() != samples_per_vector; `ZeroVectorCountSegmentRejectedAtConfigure` throws on zero vector_count.
  - Sidecar-as-physics misuse: `SidecarRoutingFieldsNotUsedAsPhysicsInputWhenCrossCheckDisabled` — tokens with deliberately wrong `pulse_range_start/count/payload_byte_count` are accepted, and the assembled frame still contains the correct vector count and samples from the typed CRSD reader result, proving physics comes exclusively from the typed reader payload.
  - Per-segment finalization: `NoOutputProducedOnDataTokensOnlyOnEos` — each of 3 data tokens returns `nullopt`; only the first EOS returns a message; subsequent EOS calls return `nullopt` (completion_emitted_ guard). No output is ever produced on a data token, making per-segment finalization semantics impossible.

### 7. Split/merge partition metadata contract is defined for PR4.
- Status: **PASS**
- Evidence:
  - `SarAperturePartition` defined in examples/SAR/include/sar/SarPhaseHistoryModel.hpp with fields: `partition_id`, `partition_count`, `global_vector_start`, `vector_count`, `ordering_key`, `input_boundary_hash`, `output_boundary_hash`.
  - `SarAperturePartitionScheme` defined with: `partitions` vector, `expected_partition_count`, `merge_ordering_key`.
  - `SarPhaseHistoryApertureFrame` carries a `partition_scheme` field.
  - `BuildApertureMessage` populates one partition per segment with: non-overlapping/non-gapped vector ranges, per-partition ordering_key == segment_index, input_boundary_hash == segment payload_hash, merge_ordering_key == ordered_set_payload_hash.
  - `PartitionSchemeIsPopulatedWithCorrectContractForPR4` asserts:
    - `scheme.expected_partition_count == 3`.
    - `scheme.merge_ordering_key != 0`.
    - `scheme.partitions.size() == 3`.
    - Vector ranges are contiguous with no gaps or overlaps.
    - All `partition.vector_count > 0` and `partition.input_boundary_hash != 0`.
    - All ordering keys are unique (deterministic merge ordering guaranteed).

### 8. No focused-image transform, Metal lane, sink, SarPy reference, local real-data workflow, or MATLAB dependency was added.
- Status: **PASS**
- Evidence:
  - Files changed in PR3 + PR3b are scoped exclusively to:
    - examples/SAR/include/sar/SarPhaseHistoryModel.hpp (model types only)
    - examples/SAR/include/sar/CrsdApertureAssemblyAdapterNode.hpp (adapter node)
    - examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp (adapter implementation)
    - examples/SAR/plugins/crsd_aperture_assembly_adapter_node_plugin.cpp (plugin facade)
    - examples/SAR/plugins/CMakeLists.txt (adapter plugin target only)
    - examples/SAR/include/sar/io/CrsdReader.hpp (channel_id field additions only)
    - examples/SAR/src/io/CrsdReader.cpp (vectors list population, no new sinks/transforms)
    - examples/SAR/test/test_crsd_aperture_assembly_adapter_node.cpp (adapter tests only)
    - examples/SAR/test/CMakeLists.txt (adapter test/plugin wiring only)
  - No `CrsdFocusedImageTransformNode`, no Metal lane nodes, no sink nodes, no SarPy/MATLAB dependencies introduced. Source-code grep for those patterns found no matches in new PR3b files.

## PR3b Verifier Failure Closure Summary

The original PR3 verifier issued FAIL on six items. PR3b closes all six:

| Original Failure | PR3b Closure | Status |
|---|---|---|
| Geometry-field presence and continuity validation | `ValidateAssemblyConsistency` checks finite rcv_time_s/position/velocity/carrier/sample_rate; signal size per vector | CLOSED |
| Explicit invariant total == sum of segments | Configure-time sum check + runtime emitted count check + `TotalVectorCountEqualsSumOfSegmentVectorsAndEmittedPayload` | CLOSED |
| Channel consistency contract and validation | `channel_id` added to reader/model; `inconsistent_channel_id` enforcement; `InconsistentChannelIdAcrossSegmentsRejectedAtConfigure` | CLOSED |
| Complete test coverage (metadata/PVP, ownership/layout, token preservation) | 6 new covering tests; field-by-field mapping, layout, and identity assertions | CLOSED |
| Negative tests (payload drop, sidecar physics, per-segment final-image) | `EmptySignal`, `TruncatedSignal`, `ZeroVectorCount`, `SidecarRoutingFieldsNotUsedAsPhysicsInput`, `NoOutputProducedOnDataTokensOnlyOnEos` | CLOSED |
| Split/merge partition metadata contract for PR4 | `SarAperturePartition`/`SarAperturePartitionScheme` defined; populated by `BuildApertureMessage`; `PartitionSchemeIsPopulatedWithCorrectContractForPR4` | CLOSED |
