# DSP FHSS Decoder PR7D Implementer Report

PR7D: Split FHSS GraphX Nodes Into Per-Node Files And Plugins

## Summary

Implemented PR7D by replacing the unified FHSS GraphX node definition header with one public header and one source translation unit per FHSS GraphX node, preserving the PR7A packet contracts and PR7B `graph::gpu::accel::ControlToken<...>` port model.

Added dynamic plugin/provider registration for every FHSS GraphX node and expanded tests to prove per-node include boundaries, deleted unified-header guardrails, token-wrapped ports, representative node behavior, and dynamic plugin loading.

## Implemented Nodes

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

## Files Added

- `libdsp/include/dsp/fhss/FHSSGraphXNodeUtils.hpp`
- `libdsp/src/dsp/FHSSGraphXNodeUtils.cpp`
- One header/source pair for each FHSS GraphX node under:
  - `libdsp/include/dsp/fhss`
  - `libdsp/src/dsp`
- One plugin source file for each FHSS GraphX node under `libdsp/plugins`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR7D.md`

## Files Updated

- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/unit/test_fhss_graphx_nodes.cpp`
- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`

## Files Deleted

- `libdsp/include/dsp/fhss/FHSSGraphXNodes.hpp`

## Validation

- Build:
  - `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
- Focused PR7D tests:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXNodeTest.*:FHSSGraphXGuardrailTest.*:FHSSGraphXPacketContractTest.*'`
  - Result: 16 passed.
- Broader FHSS regression:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXNodeTest.*:FHSSGraphXGuardrailTest.*'`
  - Result: 79 passed.

## Scope Control

No graph JSON end-to-end executor wiring, real channelizer, Metal/GPU execution, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

The deleted unified FHSS node header was not preserved as a compatibility shim.
