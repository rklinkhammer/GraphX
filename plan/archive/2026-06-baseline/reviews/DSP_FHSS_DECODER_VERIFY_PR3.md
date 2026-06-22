# DSP FHSS Decoder PR3 Verifier Report

## PR

PR3 from `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`: Detected-Pulse Metadata And Pulse Merge/Association.

## Verdict

Pass for PR3 scope.

## Findings

No blocking or non-blocking PR3 issues found.

## Required Checks

- `FHSSDetectedPulse` / `FHSSPulseCandidate` metadata includes global timing, channel timing, channel id, frequency index, RF metadata frequency, IQ offset frequency, estimated center frequency, frequency error, amplitude/power/SNR/noise floor, phase/CFO fields, bandwidth, confidence, detector id, and packet sequence where appropriate: pass.
  - `FHSSDetectedPulse` contains the required pulse metadata.
  - `FHSSLocalPulseDetection` carries local/channel metadata before normalization.
  - `FHSSPulseCandidateWithEvidence` preserves associated complex evidence.
- `global_start_sample` is derived from `input_packet_global_start_sample + local_start_offset`: pass.
  - Covered by `FHSSPulseMergeTest.NormalizesLocalTimingToSharedGlobalSampleDomain`.
- Detected pulses from multiple channels sort by `global_start_sample`: pass.
  - Covered by `FHSSPulseMergeTest.SortsDetectedPulsesByGlobalStartSample`.
- Duplicate detections on the same frequency/window retain the higher-confidence/SNR pulse: pass.
  - Covered by `FHSSPulseMergeTest.DuplicateDetectionsKeepHigherConfidenceCandidate`.
- Cross-frequency collisions follow the PR1 unsupported-overlap policy: pass.
  - Covered by `FHSSPulseMergeTest.CrossFrequencyCollisionsAreRejectedAsUnsupported`.
- Provisional slot index uses `global_start_sample / N_period`: pass.
  - Covered by `FHSSPulseMergeTest.AssignsProvisionalAndFinalSlotIndices`.
- Final slot index uses `(global_start_sample - message_epoch_sample) / N_period` when epoch is known: pass.
  - Covered by `FHSSPulseMergeTest.AssignsProvisionalAndFinalSlotIndices`.
- Missing or inconsistent global timing metadata is rejected: pass.
  - Covered by `FHSSPulseMergeTest.RejectsMissingGlobalSampleTimingMetadata`.
  - Future decimated/channelized sample-time fields are represented by `FHSSSampleTimeMap` and covered by `FHSSPulseMergeTest.SupportsFutureDecimatedSampleTimeMapping`.
- Complex evidence is preserved through the candidate stream: pass.
  - Covered by `FHSSPulseMergeTest.PreservesComplexEvidenceThroughCandidateStream`.
- No detector, CPSM decoder, word decoder, preamble detector, message assembler, graph runtime lane, Metal/GPU, or channelizer implementation was added: pass.
  - Scope scan found only FHSS protocol/generator/merge headers and unit tests.
  - No FHSS detector, decoder, message assembler, plugin, Metal/GPU, or channelizer symbols were found.

## Tests Run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: passed, no work to do.

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

Result: interrupted after remaining silent for several minutes. No failure output was produced before interruption. The PR3-focused and all-FHSS test filters passed.

## Residual Risk

- `FHSSPulseMergeNode` is currently a pure DSP helper modeling merge-node semantics, not a GraphX runtime node or plugin. That matches PR3 scope, but PR8 will still need to wire the final runtime graph.
- Full `libgraph_unit` CTest did not complete during this verifier run; targeted PR3 and all-FHSS coverage passed cleanly.
