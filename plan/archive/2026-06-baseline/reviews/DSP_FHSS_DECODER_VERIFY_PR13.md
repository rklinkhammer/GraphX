# DSP FHSS Decoder PR13 Verifier Report

PR: PerChannelPulseDetectorNode And Merge Handoff

## Verdict

PASS. PR13 is implemented within the requested scope.

## Verification Findings

- `PerChannelPulseDetectorNode` exists as a real GraphX node in
  `libdsp/include/dsp/fhss/PerChannelPulseDetectorNode.hpp`.
- The node inherits `graph::NamedInteriorNode` and uses token-wrapped PR11
  contracts:
  - input: `graph::gpu::accel::ControlToken<FHSSChannelizedIqPacket>`
  - output: `graph::gpu::accel::ControlToken<FHSSPerChannelPulseEvidencePacket>`
- The detector consumes exactly one channel packet and emits detected pulse
  metadata in shared global sample time using the existing sample-time
  normalization path.
- The detector uses the single channel metadata supplied by `ChannelizerNode`
  and rejects mismatched `channel_id != frequency_index`.
- No frequency-table scan or active-frequency ranking path was added to the
  detector.
- Complex channel evidence is preserved in `channel_iq` and pulse evidence is
  preserved for downstream CPSM branch metrics.
- Required pulse metadata is emitted:
  - frequency index
  - RF metadata frequency
  - IQ offset frequency
  - estimated center frequency
  - frequency error and CFO placeholders
  - SNR/confidence
  - detector id
  - packet sequence
  - channel id
  - global timing
- `FHSSPulseMergeNode` accepts the per-channel evidence token on a dedicated
  GraphX lane and emits ordered `FHSSPulseCandidateToken` output.
- Plugin/provider registration exists through
  `per_channel_pulse_detector_node`.
- Tests cover token type contracts, plugin/provider dynamic loading,
  per-channel metadata, timing, confidence, preserved evidence, and merge
  handoff.
- No word decoder, preamble detector, CPSM Viterbi/MLSE duplication, message
  assembly, graph JSON executor wiring, Metal/GPU execution, Doppler/noise
  behavior, or overlap-aware separation was added by PR13.

## Validation Commands

Passed:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXNodeTest.*'
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXGuardrailTest.*'
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXPacketContractTest.*'
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=*FHSS*:*CPSM*'
git diff --check
```

Results:

- `FHSSGraphXNodeTest.*`: 10 tests passed
- `FHSSGraphXGuardrailTest.*`: 12 tests passed
- `FHSSGraphXPacketContractTest.*`: 10 tests passed
- full FHSS/CPSM filter: 101 tests passed

## Residual Risk

- PR13 uses a simple deterministic pulse-evidence extraction suitable for the
  current fixture lane. Robust detection under Doppler/noise/overlap remains
  deferred by the roadmap and was not part of this PR.

## Result

PR13 is verified. The channelized graph can now place one
`PerChannelPulseDetectorNode` after each `ChannelizerNode` frequency output
port and hand detected per-channel evidence into `FHSSPulseMergeNode`.
