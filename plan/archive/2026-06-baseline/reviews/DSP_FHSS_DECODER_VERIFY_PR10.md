# DSP FHSS Decoder PR10 Verifier Report

PR: PR10 - Explicit FHSS IQ Source Message Schedule And Frequency Mapping
Role: VERIFIER
Date: 2026-06-21
Verdict: PASS

## Summary

PR10 satisfies the requested scope. The FHSS synthetic IQ source now uses an explicit configured `messages[]` schedule instead of implicit single-message/random payload selection. Each configured pulse explicitly carries its frequency index, uint32 value, and preamble/body role. IQ offsets are derived from RF metadata entries and the configured IQ center frequency, and validation preserves the PR10 limits around reserved indices, message length, overlap rejection, preamble consistency, and deterministic idle output.

No receiver acquisition, overlap-aware separation, channelizer implementation, downconverter implementation, Doppler/noise behavior, Metal/GPU execution, or production RF claim was added as part of this PR.

## Required Checks

| Check | Result | Evidence |
| --- | --- | --- |
| Source config accepts `messages[]` with multiple messages. | PASS | `FHSSGraphXConfig.hpp` parses `messages` into `std::vector<FHSSScheduledMessageSpec>` and the generator validates/emits all scheduled messages in order. |
| Each message has a stable `message_id`. | PASS | `FHSSScheduledMessageSpec` and `FHSSTruthPulse` include `message_id`; parser requires it and tests verify propagation. |
| Each message has `transmit_start_sample` or validated equivalent transmit time converted to global samples. | PASS | Parser accepts `transmit_start_sample`; generator uses it for message start timing and idle gaps. |
| Every pulse explicitly supplies `frequency_index`, `value`, and preamble/body role. | PASS | Message pulse parsing requires frequency/value and role semantics; generator no longer synthesizes payload/body pulse choices randomly. |
| Generated truth metadata matches configured message id, transmit time, global pulse start, duration, frequency index, RF metadata frequency, derived IQ offset frequency, value, and preamble flag. | PASS | Truth metadata includes these fields; focused generator tests verify timing, frequency metadata, value, preamble flag, and message id. |
| Payload/body pulse frequencies are not selected randomly. | PASS | Source/generator path consumes configured pulse lists; no random body-frequency selection is required for PR10 source generation. |
| Removed/deprecated random payload fields are no longer required by the source path. | PASS | The PR10 source config and fixture no longer require `payload_random_seed`, `payload_random_deterministic`, or `payload_values`. Legacy decode-config parsing remains outside the source path. |
| Source rejects reserved indices 0 and 63 in any transmitted pulse. | PASS | Validation routes configured preamble/body pulses through selectable-frequency checks. Existing tests cover reserved active/preamble rejection; a direct reserved body-pulse regression test would be useful but is not blocking. |
| Source rejects overlength messages above 256 pulses including preamble. | PASS | Generator validation rejects oversized scheduled messages; tests cover this case. |
| Source rejects overlapping scheduled messages. | PASS | Generator validation rejects overlapping message intervals; tests cover overlap rejection. |
| Source validates identical-frequency preamble word consistency. | PASS | Preamble consistency validation remains enforced over configured message pulses; tests cover mismatched repeated-frequency preamble words. |
| IQ offsets are derived as `rf_frequency_hz - iq_center_frequency_hz` and checked against Nyquist, occupied-bandwidth, and CFO guards. | PASS | Config parsing derives offsets from the RF table and IQ center; existing frequency-map validation guards offsets against Nyquist/bandwidth/CFO constraints. |
| Zero-message config emits deterministic idle samples according to explicit idle mode and explicit output duration. | PASS | Zero-message source config emits deterministic zero/NULL complex samples for the configured duration; tests cover this behavior. |
| Existing deterministic PR8 fixture behavior remains covered through the new explicit-message schema. | PASS | The PR8 fixture JSON was rewritten to explicit `messages[]`, and the GraphX executor test still runs the deterministic lane. |
| No receiver acquisition, overlap-aware separation, channelizer implementation, downconverter implementation, Doppler/noise behavior, Metal/GPU, or production RF claim was added. | PASS | Scope grep and code inspection found only documentation/future-boundary mentions and pre-existing non-FHSS GPU code. |

## Verification Commands

```text
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: PASS, target already up to date.

```text
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSSyntheticIqGeneratorTest.*:FHSSGraphXExecutorTest.*'
```

Result: PASS, 15 tests passed.

```text
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*' --gtest_brief=1
```

Result: PASS, 90 tests passed.

```text
git diff --check
```

Result: PASS, no whitespace errors reported.

```text
rg -n "FHSSDownconverterNode|ChannelizerNode|PerChannelPulseDetectorNode|overlap-aware|enable_noise = true|enable_doppler = true|enable_multipath = true|production RF|Metal" \
  libdsp/include/dsp/fhss libdsp/src/dsp libdsp/config/fhss_cpsm_fixture_500msps.json \
  libgraph/test/unit/test_fhss_* docs/dsp/fhss_decoder.md
```

Result: PASS for PR10 scope. Matches were limited to documentation/future-boundary text, explicit negative tests, and unrelated pre-existing non-FHSS GPU/Metal code.

## Non-Blocking Coverage Notes

- Add a positive config test with two non-overlapping scheduled messages to make the multi-message acceptance path explicit in tests.
- Add a direct body-pulse reserved-index rejection test for configured message pulses. Current validation covers the path, but the regression test would make the PR10 contract clearer.

## Final Assessment

PR10 is verified as implemented within scope. The explicit source message schedule, derived IQ offset mapping, deterministic idle behavior, and PR8 fixture migration are consistent with the roadmap requirements.
