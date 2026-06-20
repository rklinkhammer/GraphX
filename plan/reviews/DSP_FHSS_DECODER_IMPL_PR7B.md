# DSP FHSS Decoder PR7B Implementer Report

## PR

PR7B: Replace FHSS Pseudo-Nodes With Real GraphX Nodes

## Scope Implemented

- Added real FHSS GraphX nodes for the CPU lane:
  - `FHSSSyntheticIqSourceNode`
  - `FHSSCorrelatorBankDetectorNode`
  - `FHSSPulseMergeNode`
  - `FHSSPulseCandidateNode`
  - `CPSMBranchMetricNode`
  - `CPSMViterbiDecoderNode`
  - `FHSSPulseWordDecoderNode`
  - `FHSSPreambleDetectorNode`
  - `FHSSMessageAssemblerNode`
  - `FHSSMessageSinkNode`
- Every FHSS GraphX node port uses `graph::gpu::accel::ControlToken<...>`.
- PR7A packet contracts are carried as token sidecars/payloads, not raw GraphX node port payloads.
- Renamed the old public pseudo-node helper classes into non-node algorithm kernels:
  - `FHSSCorrelatorBankDetectorKernel`
  - `FHSSPulseMergeKernel`
  - `CPSMBranchMetricKernel`
  - `CPSMViterbiDecoderKernel`
  - `FHSSPulseWordDecoderKernel`
  - `FHSSPreambleDetectorKernel`
  - `FHSSMessageAssemblerKernel`
  - `FHSSMessageSinkKernel`
- Added GraphX node tests that exercise the FHSS lane through `Produce`, `Transfer`, and `Consume`.
- Added compile-time type-contract tests proving FHSS node input/output port types are token-wrapped and accel-ready.
- Preserved PR1-PR7 deterministic behavior through the existing algorithm coverage and the new GraphX node boundary tests.

## Files Added

- `libdsp/include/dsp/fhss/FHSSGraphXNodes.hpp`
- `libgraph/test/unit/test_fhss_graphx_nodes.cpp`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR7B.md`

## Files Updated

- `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetector.hpp`
- `libdsp/include/dsp/fhss/FHSSCpsmDecoder.hpp`
- `libdsp/include/dsp/fhss/FHSSGraphXPackets.hpp`
- `libdsp/include/dsp/fhss/FHSSMessageAssembly.hpp`
- `libdsp/include/dsp/fhss/FHSSPulseMerge.hpp`
- `libdsp/include/dsp/fhss/FHSSPulseWordDecoder.hpp`
- `libgraph/test/unit/test_fhss_correlator_bank_detector.cpp`
- `libgraph/test/unit/test_fhss_cpsm_decoder.cpp`
- `libgraph/test/unit/test_fhss_graphx_packets.cpp`
- `libgraph/test/unit/test_fhss_message_assembly.cpp`
- `libgraph/test/unit/test_fhss_pulse_merge.cpp`
- `libgraph/test/unit/test_fhss_pulse_word_decoder.cpp`

## Plugin/Provider Scope

No FHSS nodes were exposed through the plugin path in PR7B, so no plugin/provider registration tests were added.

## Guardrails

- Did not keep compatibility shims for old pseudo-node static APIs.
- Did not add graph JSON end-to-end executor wiring.
- Did not add a real channelizer.
- Did not add Metal/GPU execution.
- Did not add Doppler/noise behavior.
- Did not add overlap-aware separation.
- Did not make production RF claims.

## Validation

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
  - Passed.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*'`
  - Passed: 9 tests.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXNodeTest.*'`
  - Passed: 72 tests.
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`
  - Passed: 1/1 test, 80.58 sec.

## Notes For PR7C

PR7C can add broader guardrails against future public FHSS pseudo-node scaffolding. PR7B already removes the old public pseudo-node class names by replacing them with real GraphX nodes and non-node kernel names.
