# DSP FHSS Decoder PR13 Implementer Report

PR: PerChannelPulseDetectorNode And Merge Handoff

## Summary

Implemented PR13 by adding a real GraphX `PerChannelPulseDetectorNode` for
one channelized IQ stream from one `ChannelizerNode` output port. The node
consumes `graph::gpu::accel::ControlToken<FHSSChannelizedIqPacket>` and emits
`graph::gpu::accel::ControlToken<FHSSPerChannelPulseEvidencePacket>` with
single-channel pulse evidence in the shared global sample-time domain.

## Changes

- Added `PerChannelPulseDetectorNode` as a per-node header/source pair:
  - `libdsp/include/dsp/fhss/PerChannelPulseDetectorNode.hpp`
  - `libdsp/src/dsp/PerChannelPulseDetectorNode.cpp`
- Added `PerChannelPulseDetectorConfig` with focused detector metadata and
  threshold configuration.
- Implemented the detector as a real GraphX interior node:
  - input: `ControlToken<FHSSChannelizedIqPacket>`
  - output: `ControlToken<FHSSPerChannelPulseEvidencePacket>`
  - no raw FHSS packet types are exposed as node port types
- The detector uses only the supplied channel metadata:
  - frequency index
  - channel id
  - RF metadata frequency
  - IQ offset frequency
  - channel sample-rate/decimation/sample-time map
- The detector does not scan or rank across frequencies.
- The detector preserves channelized/dehopped complex evidence for downstream
  CPSM branch metrics.
- The detector emits pulse metadata including:
  - global start/end/duration
  - channel start sample and channel id
  - frequency index
  - RF metadata frequency
  - IQ offset frequency
  - estimated center frequency
  - frequency error/CFO placeholders
  - amplitude, power, SNR, noise floor
  - phase at start and phase slope
  - bandwidth and confidence
  - detector id and packet sequence
- Extended `FHSSPulseMergeNode` with a second GraphX input/output lane so
  `FHSSPerChannelPulseEvidenceToken` can merge directly into ordered
  `FHSSPulseCandidateToken` output while preserving the existing PR4
  correlator-bank detected-pulse lane.
- Added `per_channel_pulse_detector_node` plugin/provider registration.
- Updated FHSS GraphX guardrails so the new node must remain in its own
  header/source and must inherit real GraphX node bases.
- Added focused GraphX node tests for:
  - token-wrapped port types
  - PR11 packet contracts
  - plugin/provider dynamic loading
  - single-channel metadata use
  - global timing and decimated channel handoff
  - confidence/SNR metadata
  - preserved complex evidence
  - direct merge handoff through `FHSSPulseMergeNode`

## Scope Notes

- No word decoding was added.
- No preamble detection was added.
- No CPSM Viterbi/MLSE duplication was added.
- No message assembly or graph JSON end-to-end executor wiring was added.
- No Metal/GPU execution, Doppler/noise behavior, or overlap-aware separation
  was added.

## Validation

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

## Result

PR13 is implemented and validated. The FHSS channelized-lane graph can now
instantiate one `PerChannelPulseDetectorNode` per `ChannelizerNode` frequency
output port and hand per-channel pulse evidence into `FHSSPulseMergeNode`.
