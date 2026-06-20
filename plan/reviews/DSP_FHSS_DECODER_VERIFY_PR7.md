# DSP FHSS Decoder PR7 Verifier Report

Role: VERIFIER

PR: PR7 - Hop-Only Preamble Detector And Message Assembler

Verdict: Pass

## Files Reviewed

- `plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md`
- `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`
- `libdsp/include/dsp/fhss/FHSSMessageAssembly.hpp`
- `libgraph/test/unit/test_fhss_message_assembly.cpp`

## Required Checks

- Pass: Preamble lock is hop-only over exactly 16 pulses. `FHSSPreambleDetectorNode` checks the first 16 decoded pulse frequency indices against the configured preamble hop pattern.
- Pass: Word mismatches do not prevent preamble lock. The PR7 test mutates decoded preamble values and still locks while reporting truth mismatches.
- Pass: Identical preamble frequencies still require identical fixture word values via `ValidatePreambleWordConsistency`.
- Pass: Active set after lock has exactly four selectable frequencies via `ValidateActiveFrequencySet`.
- Pass: Payload frequencies outside the active set are rejected with `PayloadFrequencyRejected`.
- Pass: Total message length over 256 pulses including preamble is rejected with `MessageTooLong`.
- Pass: Missing preamble is rejected with `MissingPreamble`.
- Pass: Message assembly operates on globally ordered decoded pulses by sorting on `global_start_sample`.
- Pass: Truth comparator reports mismatched start sample, duration, frequency, and decoded value.
- Pass: Diagnostics include `pulse_count`, `rejected_count`, `preamble_lock`, and `truth_mismatch_count`.
- Pass: Overlapped messages remain unsupported and are rejected deterministically with `UnsupportedOverlap`.
- Pass: No graph runtime integration, real channelizer, Metal/GPU path, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

## Technical Notes

- `FHSSPreambleDetectorNode` does not inspect decoded word values for preamble lock; only hop sequence is used.
- `FHSSMessageAssemblerNode` separates preamble and payload after lock and validates payload frequencies against the locked active set.
- `FHSSMessageSinkNode` is a scoped diagnostics extraction helper, not a graph runtime/plugin sink.
- Rejection paths set diagnostics with `pulse_count`, `rejected_count`, and `preamble_lock = false`.

## Tests Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSMessageAssemblyTest.*'`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*'`
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`

All tests passed.

## Findings

- None.
