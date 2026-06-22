# DSP FHSS Decoder PR12 Implementer Report

PR: FHSS DownconverterNode And Frequency-Parallel CPU ChannelizerNode

## Summary

Implemented PR12 by adding real GraphX `FHSSDownconverterNode` and
`ChannelizerNode` components. Both nodes use `graph::gpu::accel::ControlToken`
edge types carrying FHSS packet sidecars. The downconverter supports validated
passthrough and declared frequency translation, while the channelizer emits one
explicit channel packet per configured FHSS frequency index in a token-wrapped
channelized stream sidecar.

## Changes

- Added `FHSSDownconverterNode`:
  - real `graph::NamedInteriorNode`
  - input: `FHSSSyntheticIqToken`
  - output: `FHSSDownconvertedIqToken`
  - validates passthrough when input/output IQ reference frames match
  - validates translated mode with:
    - input IQ center/reference frequency
    - output/channelizer IQ center/reference frequency
    - declared translation frequency
    - negative-exponential phase convention
  - rejects implicit or inconsistent frequency-frame mismatches
  - translates by the declared IQ center delta, not by absolute 1 GHz RF metadata
  - preserves complex IQ evidence and global sample timing metadata
- Added `ChannelizerNode`:
  - real `graph::NamedInteriorNode`
  - input: `FHSSDownconvertedIqToken`
  - output: `FHSSChannelizedIqStreamToken`
  - emits one `FHSSChannelizedIqPacket` per configured FHSS frequency index
  - preserves RF metadata frequency, IQ offset frequency, channel id, channel
    sample rate, decimation factor, group delay, and input global sample origin
  - allows reserved indices `0` and `63` as receiver guard/metadata channels
  - rejects reserved indices `0` and `63` as transmitted active/pulse
    frequencies
  - rejects duplicate receiver frequency indices and duplicate channel ids
- Added `FHSSChannelizedIqStreamPacket` and `FHSSChannelizedIqStreamToken` so
  the GraphX edge can carry the full frequency-parallel channel packet set
  without making raw FHSS packet types node port types.
- Added per-node source files:
  - `libdsp/src/dsp/FHSSDownconverterNode.cpp`
  - `libdsp/src/dsp/ChannelizerNode.cpp`
- Added plugin/provider wrappers:
  - `fhss_downconverter_node_plugin.cpp`
  - `channelizer_node_plugin.cpp`
- Added plugin CMake wiring for both nodes.
- Updated FHSS GraphX guardrails so the new PR12 nodes remain subject to the
  per-node header/source invariant.
- Added focused GraphX tests for:
  - token-wrapped port contracts
  - plugin/provider dynamic loading
  - downconverter passthrough
  - downconverter frequency translation
  - rejection of implicit frequency-frame mismatches
  - channel count equals configured 64-entry frequency count
  - one channel packet per frequency index
  - reserved receiver guard channels
  - duplicate receiver/channel-id rejection
  - channel metadata preservation

## Scope Notes

- The channelizer is fixture-grade CPU mixing/decimation metadata plumbing. It
  does not claim production channelizer filter quality.
- The PR8 correlator-bank graph was not replaced.
- No per-channel pulse detector implementation was added.
- No graph JSON end-to-end executor wiring was added.
- No real RF capture, Metal/GPU execution, Doppler/noise behavior,
  overlap-aware separation, or production RF claim was added.

## Validation

Passed:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXNodeTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXPacketContractTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXGuardrailTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*' --gtest_brief=1
git diff --check
```

Results:

- `FHSSGraphXNodeTest.*`: 9 tests passed
- `FHSSGraphXPacketContractTest.*`: 10 tests passed
- focused node/packet/guardrail filter: 30 tests passed
- full FHSS/CPSM filter: 99 tests passed

## Result

PR12 is implemented and validated as a CPU-only, contract-preserving step
toward the channelized FHSS receiver lane.
