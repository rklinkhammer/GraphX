# DSP FHSS Decoder PR8 Implementer Report

PR8: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics

## Summary

Implemented PR8 by adding a repository-consistent FHSS CPSM fixture graph JSON and an end-to-end GraphX executor test that loads the FHSS nodes through the plugin/provider path using `GraphExecutorBuilder`.

The graph runs the deterministic CPU FHSS lane from synthetic IQ through correlator-bank detection, merge, candidate boundary, CPSM branch metrics, Viterbi symbol decisions, pulse-word decoding, hop-only preamble lock, message assembly, truth comparison, and message sink diagnostics.

## Key Implementation Notes

- Added `libdsp/config/fhss_cpsm_fixture_500msps.json`.
- Added JSON configuration support for the FHSS source, detector, preamble detector, and message assembler through existing `graph::IConfigurable` / `graph::IParameterized` methods.
- Kept all FHSS node ports as `graph::gpu::accel::ControlToken<...>` sidecar packets.
- Extended the CPSM branch-metric and symbol-decision packets to carry full-message pulse batches while preserving existing first-pulse fields.
- Updated `FHSSPulseWordDecoderNode` to emit a token-wrapped `FHSSDecodedPulseWordsPacket`, allowing the JSON graph to wire directly into the preamble detector.
- Updated `FHSSMessageAssemblerNode` to consume the preamble detector's assembled-message token and perform final assembly/truth comparison from the globally ordered decoded pulses.
- Added sink diagnostics through existing `graph::IDiagnosable` and completion signaling through the existing GraphX completion callback pattern.

## Files Added

- `libdsp/include/dsp/fhss/FHSSGraphXConfig.hpp`
- `libdsp/config/fhss_cpsm_fixture_500msps.json`
- `libgraph/test/unit/test_fhss_graphx_executor.cpp`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR8.md`

## Files Updated

- `libdsp/include/dsp/fhss/FHSSGraphXPackets.hpp`
- `libdsp/include/dsp/fhss/FHSSGraphXNodeUtils.hpp`
- `libdsp/include/dsp/fhss/FHSSSyntheticIqSourceNode.hpp`
- `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetectorNode.hpp`
- `libdsp/include/dsp/fhss/CPSMBranchMetricNode.hpp`
- `libdsp/include/dsp/fhss/CPSMViterbiDecoderNode.hpp`
- `libdsp/include/dsp/fhss/FHSSPulseWordDecoderNode.hpp`
- `libdsp/include/dsp/fhss/FHSSPreambleDetectorNode.hpp`
- `libdsp/include/dsp/fhss/FHSSMessageAssemblerNode.hpp`
- `libdsp/include/dsp/fhss/FHSSMessageSinkNode.hpp`
- `libgraph/test/unit/test_fhss_graphx_nodes.cpp`

## Validation

- Build:
  - `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
- Focused PR8/GraphX regression:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXExecutorTest.*:FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXGuardrailTest.*'`
  - Result: 18 passed.
- Broad FHSS regression:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*'`
  - Result: 81 passed.

## Scope Control

No real RF capture, external dataset path, real channelizer topology, Metal/GPU execution, production RF claim, Doppler/noise behavior, overlap-aware separation, or optional PDW diagnostics were added.

The PR8 executor test uses real GraphX nodes, PR7A packet contracts, token-wrapped FHSS edges, PR7D plugin/provider loading, and `GraphExecutorBuilder`; it does not reference deleted pre-GraphX pseudo-node helpers or the deleted unified FHSS node-definition header.
