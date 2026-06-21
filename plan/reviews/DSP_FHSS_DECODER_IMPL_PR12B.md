# DSP FHSS Decoder PR12B Implementer Report

PR: Correct Channelizer Graph Shape To 64 Output Ports

## Summary

Implemented PR12B by correcting `ChannelizerNode` so the
channel-per-frequency invariant is represented by GraphX output ports rather
than an aggregate sidecar payload. `ChannelizerNode` now exposes exactly 64
GraphX output ports, with output port `N` producing
`ControlToken<FHSSChannelizedIqPacket>` for frequency index/channel id `N`.

## Changes

- Removed the canonical aggregate channelizer output contract:
  - deleted `FHSSChannelizedIqStreamPacket`
  - deleted `FHSSChannelizedIqStreamToken`
  - removed `ChannelizedIqStream` from the FHSS GraphX edge contract list
- Reworked `ChannelizerNode` from a single-output interior node into a
  repository-consistent one-input/many-output GraphX node:
  - consumes one `FHSSDownconvertedIqToken`
  - exposes 64 `FHSSChannelizedIqToken` output ports
  - `OutputType<0>`, `OutputType<1>`, `OutputType<62>`, and
    `OutputType<63>` are all `ControlToken<FHSSChannelizedIqPacket>`
  - output port `N` maps to `frequency_index == N` and `channel_id == N`
- Preserved PR12 channel packet generation:
  - RF metadata frequency
  - IQ offset frequency
  - channel id
  - channel sample rate
  - decimation factor
  - group delay
  - input global sample origin
  - complex IQ evidence
- Preserved reserved receiver guard/metadata output ports `0` and `63` while
  keeping indices `0` and `63` invalid for transmitted active/pulse
  frequencies.
- Rewrote tests that previously accepted one edge carrying all 64 channels.
- Added guardrail coverage preventing aggregate channelizer output contracts
  from returning as canonical GraphX node port types.
- Preserved `FHSSDownconverterNode` passthrough and declared
  frequency-translation behavior.
- Preserved plugin/provider dynamic loading for the corrected `ChannelizerNode`.

## Scope Notes

- No per-channel pulse detector implementation was added.
- No graph JSON end-to-end executor wiring was added.
- No real RF capture or production channelizer claim was added.
- No Metal/GPU execution, Doppler/noise behavior, or overlap-aware separation
  was added.
- The historical PR12 implementer report still describes the earlier aggregate
  stream shape; PR12B supersedes that shape in code and tests.

## Validation

Passed:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXNodeTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXPacketContractTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXGuardrailTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXGuardrailTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*' --gtest_brief=1
git diff --check
```

Results:

- `FHSSGraphXNodeTest.*`: 9 tests passed
- `FHSSGraphXPacketContractTest.*`: 10 tests passed
- `FHSSGraphXGuardrailTest.*`: 12 tests passed
- focused node/packet/guardrail filter: 31 tests passed
- full FHSS/CPSM filter: 100 tests passed

## Result

PR12B is implemented and validated. The channelizer graph shape now supports
one downstream per-channel detector per configured FHSS frequency output port.
