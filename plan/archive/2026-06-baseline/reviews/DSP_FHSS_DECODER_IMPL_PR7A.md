# DSP FHSS Decoder PR7A Implementer Report

## PR

PR7A: FHSS GraphX Edge Contracts And Accel-Ready Data Model

## Scope Implemented

- Added canonical GraphX FHSS edge packet/contract types for:
  - synthetic IQ output
  - detected pulse evidence
  - pulse candidate evidence
  - CPSM branch metrics
  - CPSM symbol decisions
  - decoded pulse words
  - assembled messages
  - diagnostics
- Added explicit complex IQ evidence ownership/reference semantics for GraphX edges:
  - host shared immutable complex IQ samples
  - external immutable reference placeholder
  - future accelerator-token sidecar placeholder
- Preserved global sample timing, channel-local timing, RF metadata frequency, IQ offset frequency, estimated center frequency, frequency error, confidence, phase/CFO, and future sample-time mapping fields in the packet model.
- Documented the future accelerator-token/sidecar compatibility boundary as CPU-only semantic metadata.
- Added focused compile/runtime contract tests.

## Files Added

- `libdsp/include/dsp/fhss/FHSSGraphXPackets.hpp`
- `libgraph/test/unit/test_fhss_graphx_packets.cpp`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR7A.md`

## Guardrails

- Did not convert helper pseudo-nodes into GraphX runtime nodes.
- Did not add graph JSON.
- Did not add plugin runtime wiring.
- Did not add a real channelizer.
- Did not add Metal/GPU execution.
- Did not add Doppler/noise behavior.
- Did not add overlap-aware separation.
- Did not make production RF claims.

## Validation

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
  - Passed.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXPacketContractTest.*'`
  - Passed: 6 tests.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*:FHSSGraphXPacketContractTest.*'`
  - Passed: 69 tests.
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`
  - Passed: 1/1 test, 80.52 sec.

## Notes For PR7B

PR7B should replace the helper-era public `Node` suffix classes with real GraphX runtime nodes that consume and emit these packet contracts. The PR7A packet contracts should be treated as the canonical FHSS edge boundary.
