# DSP FHSS Decoder PR3 Implementer Report

## PR

PR3 from `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`: Detected-Pulse Metadata And Pulse Merge/Association.

## Files Changed

- `libdsp/include/dsp/fhss/FHSSProtocol.hpp`
  - Added PR3 validation/status codes for invalid global timing, duplicate pulse handling, and unsupported overlap reporting.
- `libdsp/include/dsp/fhss/FHSSPulseMerge.hpp`
  - Added `FHSSSampleTimeMap` for shared input sample-time mapping.
  - Added `FHSSComplexEvidence` for preserving complex sample references through association.
  - Added `FHSSLocalPulseDetection` for per-channel/per-frequency local detections.
  - Added `FHSSPulseCandidateWithEvidence`, rejection reason/report types, merge config, and merge result.
  - Added local-to-global normalization with `global_start_sample = input_packet_global_start_sample + local_start_offset` for the PR1 mapping.
  - Added future decimated/channelized sample-time mapping fields: `output_start_sample`, `decimation_factor`, and `group_delay_input_samples`.
  - Added `FHSSPulseMergeNode::Merge` semantics for sorting, duplicate rejection/replacement, unsupported cross-frequency collision reporting, slot indexing, and ordered candidate emission.
- `libgraph/test/unit/test_fhss_pulse_merge.cpp`
  - Added focused PR3 merge/association tests.

## Files Deleted

- None.

## Tests Added

- `FHSSPulseMergeTest.NormalizesLocalTimingToSharedGlobalSampleDomain`
- `FHSSPulseMergeTest.SupportsFutureDecimatedSampleTimeMapping`
- `FHSSPulseMergeTest.RejectsMissingGlobalSampleTimingMetadata`
- `FHSSPulseMergeTest.RejectsInvalidComplexEvidenceRange`
- `FHSSPulseMergeTest.SortsDetectedPulsesByGlobalStartSample`
- `FHSSPulseMergeTest.DuplicateDetectionsKeepHigherConfidenceCandidate`
- `FHSSPulseMergeTest.CrossFrequencyCollisionsAreRejectedAsUnsupported`
- `FHSSPulseMergeTest.AssignsProvisionalAndFinalSlotIndices`
- `FHSSPulseMergeTest.PreservesComplexEvidenceThroughCandidateStream`

## Tests Removed

- None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSPulseMergeTest.*'
```

Result: passed, 9 tests.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*'
```

Result: passed, 30 tests.

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure
```

Result: passed.

## Implementation Notes

- `FHSSPulseMergeNode` is a pure DSP helper class that models merge-node semantics without adding a GraphX runtime node or plugin.
- Channel-local timing is accepted only when an `FHSSSampleTimeMap` includes valid shared input global sample timing.
- Duplicate detections are defined as overlapping detections on the same frequency index; the higher-confidence candidate wins, with SNR used as the tie-breaker.
- Cross-frequency overlapping detections are rejected and reported as unsupported PR1 overlap behavior.
- Provisional slot index uses `global_start_sample / N_period`.
- Final slot index uses `(global_start_sample - message_epoch_sample) / N_period` when a message epoch is known.

## Scope Guardrails

- No detector was added.
- No CPSM decoder, word decoder, preamble detector, message assembler, or graph runtime lane was added.
- No plugin runtime, channelizer implementation, Metal/GPU, or production RF behavior was added.

## Remaining Follow-Up Work

- PR4 can add the correlator-bank detector that emits `FHSSLocalPulseDetection` or equivalent detected-pulse metadata.
- PR5 can consume ordered candidates and complex evidence for CPSM branch metrics and Viterbi/MLSE.
