# DSP FHSS Decoder PR7 Implementer Report

Role: IMPLEMENTER

PR: PR7 - Hop-Only Preamble Detector And Message Assembler

Verdict: Implemented

## Files Changed

- `libdsp/include/dsp/fhss/FHSSMessageAssembly.hpp`
- `libgraph/test/unit/test_fhss_message_assembly.cpp`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR7.md`

## Files Deleted

- None.

## Tests Added

- `FHSSMessageAssemblyTest.LocksHopOnlyPreambleOverSixteenPulses`
- `FHSSMessageAssemblyTest.WordMismatchesDoNotPreventHopOnlyLock`
- `FHSSMessageAssemblyTest.IdenticalPreambleFrequenciesRequireIdenticalFixtureWords`
- `FHSSMessageAssemblyTest.RejectsInvalidActiveSetAfterLock`
- `FHSSMessageAssemblyTest.RejectsPayloadFrequencyOutsideActiveSet`
- `FHSSMessageAssemblyTest.RejectsMessageLongerThanTwoHundredFiftySixPulses`
- `FHSSMessageAssemblyTest.RejectsMissingPreamble`
- `FHSSMessageAssemblyTest.OperatesOnGloballyOrderedDecodedPulses`
- `FHSSMessageAssemblyTest.TruthComparatorReportsStartDurationFrequencyAndValueMismatches`
- `FHSSMessageAssemblyTest.SinkReportsMinimumDiagnostics`
- `FHSSMessageAssemblyTest.RejectsPr1OverlappedMessagesDeterministically`

## Implementation Summary

- Added `FHSSPreambleDetectorNode` for hop-only lock over exactly 16 preamble pulses.
- Added `FHSSMessageAssemblerNode` for globally ordered decoded pulse assembly.
- Added `FHSSMessageSinkNode` diagnostics extraction helper.
- Treats decoded preamble word mismatches as non-locking truth/consistency diagnostics; lock is based on hop sequence only.
- Validates fixture preamble word consistency for identical preamble frequencies.
- Enforces post-lock active set as exactly four selectable frequency indices.
- Rejects payload frequencies outside the locked active set.
- Rejects messages longer than 256 pulses including preamble.
- Rejects missing preamble and overlapped pulses deterministically.
- Adds truth comparison hooks for start sample, duration, frequency index, and decoded value mismatches.
- Emits minimum diagnostics:
  - `pulse_count`
  - `rejected_count`
  - `preamble_lock`
  - `truth_mismatch_count`

## Verification Run By Implementer

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSMessageAssemblyTest.*'`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*'`
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`

All commands passed.

## Scope Notes

- No graph runtime integration, real channelizer, Metal/GPU path, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.
- CMake test wiring is via the existing `libgraph/test/CMakeLists.txt` `CONFIGURE_DEPENDS` unit-test glob; the build reran CMake and included the new test file.
