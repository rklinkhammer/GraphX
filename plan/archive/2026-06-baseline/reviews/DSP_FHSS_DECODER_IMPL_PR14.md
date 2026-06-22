# DSP FHSS Decoder PR14 Implementer Report

## PR

PR14: Channelized FHSS Graph JSON And Executor Test

## Summary

Implemented the alternate channelized deterministic FHSS GraphX fixture lane.
The new graph config keeps the PR8 correlator-bank fixture available and adds a
CPU-only channelized path:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
  -> ChannelizerNode
  -> PerChannelPulseDetectorNode[64]
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

## Implemented

- Added `libdsp/config/fhss_cpsm_channelized_fixture_500msps.json`.
- Wired the source through `FHSSDownconverterNode` before channelization as
  validated passthrough.
- Instantiated one `PerChannelPulseDetectorNode` per configured frequency
  index, with exactly 64 detector nodes.
- Wired `ChannelizerNode` output port `N` to detector `N`.
- Extended `FHSSPulseMergeNode` to expose one PR8 correlator input plus 64
  per-channel detector inputs, with detector `N` connected to merge input
  `N + 1`.
- Kept the PR8 correlator-bank config and executor test available.
- Preserved channelizer, downconverter, global timing, RF metadata frequency,
  IQ offset frequency, group delay, decimation, and channel id metadata through
  diagnostics.
- Preserved complex evidence slices through merge, CPSM branch metric, Viterbi,
  pulse-word decode, preamble, assembler, and sink boundaries.
- Added channelized topology and end-to-end executor coverage through
  `GraphExecutorBuilder` and existing plugin/provider loading.
- Updated FHSS docs to describe the PR14 channelized CPU fixture and future
  production-channelizer boundary.

## Additional Correctness Fixes Needed By PR14

- Fixed FHSS GraphX parameter metadata helpers so dynamic schema validation does
  not return `JsonView` references to local temporary JSON objects.
- Aligned `ChannelizerNode` parameter metadata with its accepted JSON schema.
- Updated guardrails for the explicit `FHSSPulseMergeNode` sink/source GraphX
  shape.
- Updated the per-channel detector unit fixture to provide coherent channelized
  evidence for the selected channel frequency.

## Out Of Scope

No real RF capture, production channelizer separation claim, external dataset,
Metal/GPU execution, Doppler/noise behavior, overlap-aware separation, graph
adaptor/accessor invention, or canonical PDW diagnostics were added.

## Validation

Passed:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSGraphXExecutorTest.*'

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSGraphXNodeTest.*:FHSSGraphXGuardrailTest.*:FHSSGraphXPacketContractTest.*'

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=*FHSS*:*CPSM*'

git diff --check
```

