# DSP FHSS Decoder PR10 Implementer Report

PR: Explicit FHSS IQ Source Message Schedule And Frequency Mapping

## Summary

Implemented PR10 by replacing the FHSS synthetic IQ source/generator path with an explicit configured message schedule. The source now consumes `messages[]`, where each message has a stable `message_id`, `transmit_start_sample`, and ordered pulse list with explicit `frequency_index`, `value`, and preamble/body role.

## Changes

- Added scheduled message protocol types:
  - `FHSSMessagePulseRole`
  - `FHSSMessagePulseSpec`
  - `FHSSScheduledMessageSpec`
- Added `message_id` to `FHSSTruthPulse`.
- Reworked `FHSSSyntheticIqGeneratorConfig` to use `messages[]` and `idle_duration_samples` instead of `payload_values`.
- Reworked synthetic IQ generation to place pulses on a global sample timeline, zero-fill idle gaps, support nonzero transmit starts, and support zero-message idle output.
- Added validation for scheduled messages:
  - 16 preamble pulses at the start of each message
  - body role after preamble
  - selectable/non-reserved pulse frequencies
  - payload/body frequencies restricted to the active set
  - maximum 256 pulses including preamble
  - identical-frequency preamble word consistency
  - PR10 overlap rejection
- Added JSON support for:
  - `messages[]`
  - `message_id`
  - `transmit_start_sample`
  - pulse `role`
  - `idle_mode`
  - `idle_duration_samples`
  - `iq_center_frequency_hz`
- Added derived IQ offset support:

```text
iq_offset_frequency_hz = rf_frequency_hz - iq_center_frequency_hz
```

- Rewrote `libdsp/config/fhss_cpsm_fixture_500msps.json` to use the explicit message schedule.
- Updated the bundled fixture active set to `[24, 28, 32, 36]` so derived IQ offsets from `iq_center_frequency_hz = 1240000000.0` remain valid under the 500 Msps Nyquist guard.
- Updated docs to describe the explicit source message schema and derived IQ offset model.
- Updated FHSS tests that generated synthetic IQ to use explicit scheduled messages.

## Scope Notes

- No receiver acquisition was added.
- No overlap-aware separation was added.
- No channelizer or downconverter implementation was added.
- No Doppler/noise/multipath behavior was added.
- No Metal/GPU execution or production RF claim was added.
- Legacy PR1 decode-config fields for deterministic payload random selection remain in `FHSSDecodeConfigFromJson` for older decode configuration semantics, but the source/generator path and fixture JSON no longer depend on them.

## Validation

Passed:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSSyntheticIqGeneratorTest.*:FHSSGraphXExecutorTest.*'
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*'
git diff --check
```

## Result

PR10 is implemented and validated for the deterministic CPU FHSS fixture lane.
