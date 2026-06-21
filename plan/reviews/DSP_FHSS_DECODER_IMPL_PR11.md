# DSP FHSS Decoder PR11 Implementer Report

PR: FHSS Channelizer And Per-Channel Edge Contracts

## Summary

Implemented PR11 by adding the data-only GraphX FHSS edge contracts needed for the future downconverter, channelizer, and per-channel pulse detector path. The contracts preserve complex IQ evidence, global sample-time mapping, RF metadata frequency, IQ offset frequency, and explicit channel metadata while keeping all node edges token-wrapped through `graph::gpu::accel::ControlToken<...>`.

## Changes

- Added new FHSS GraphX edge contract descriptors:
  - `DownconvertedIq`
  - `ChannelizedIq`
  - `PerChannelPulseEvidence`
- Added downconverter contract metadata:
  - input IQ center/reference frequency
  - output/channelizer center/reference frequency
  - translation frequency
  - passthrough flag
  - phase convention
  - sample rate
  - preserved input/output global sample origins
  - sample-time map
- Added channelizer/per-channel contract metadata:
  - channel id
  - frequency index
  - RF metadata frequency
  - IQ offset frequency
  - channel sample rate
  - decimation factor
  - filter group delay
  - input global sample origin
  - channel global sample origin
  - sample-time map
- Added packet contracts:
  - `FHSSDownconvertedIqPacket`
  - `FHSSChannelizedIqPacket`
  - `FHSSPerChannelPulseEvidencePacket`
- Added contract helpers for:
  - downconverter metadata validation
  - channel count equals configured FHSS frequency count
  - channel metadata matching one RF frequency-map entry
  - reconstructing input/global sample time from a channel sample index
- Added token aliases:
  - `FHSSDownconvertedIqToken`
  - `FHSSChannelizedIqToken`
  - `FHSSPerChannelPulseEvidenceToken`
- Added focused GraphX packet contract tests proving:
  - new packet contracts are default constructible
  - new edge types are `graph::gpu::accel::ControlToken<...>` sidecars
  - downconverter passthrough and translation metadata are explicit
  - channel count equals the configured 64-entry FHSS frequency table
  - channel id maps one-to-one with frequency index
  - reserved indices `0` and `63` are valid receiver guard/metadata channels while remaining non-selectable transmit indices
  - complex IQ evidence and sample-time mapping survive channelized and per-channel detector contracts

## Scope Notes

- No downconverter DSP implementation was added.
- No channelizer DSP implementation was added.
- No per-channel detector DSP implementation was added.
- No graph JSON or executor wiring was added.
- No Metal/GPU execution was added.
- No Doppler/noise behavior, overlap-aware separation, or production RF claim was added.
- PR10 explicit source message scheduling exists and was not changed by this PR.

## Validation

Passed:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXPacketContractTest.*' --gtest_brief=1
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*' --gtest_brief=1
git diff --check
```

## Result

PR11 is implemented and validated as a contract-only step toward the future downconverter, channelizer, and per-channel pulse detector lane.
