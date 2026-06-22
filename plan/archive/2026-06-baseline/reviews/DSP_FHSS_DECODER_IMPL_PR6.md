# DSP FHSS Decoder PR6 Implementer Report

Role: IMPLEMENTER

PR: PR6 - FHSS Pulse Word Decoder

Verdict: Implemented

## Files Changed

- `libdsp/include/dsp/fhss/FHSSPulseWordDecoder.hpp`
- `libgraph/test/unit/test_fhss_pulse_word_decoder.cpp`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR6.md`

## Files Deleted

- None.

## Tests Added

- `FHSSPulseWordDecoderTest.MapsCpsmSymbolsToBits`
- `FHSSPulseWordDecoderTest.AssemblesMsbFirstThirtyTwoBitWord`
- `FHSSPulseWordDecoderTest.KnownSymbolVectorsRecoverExpectedValues`
- `FHSSPulseWordDecoderTest.PreservesPulseMetadataAndConfidence`
- `FHSSPulseWordDecoderTest.ReportsInvalidViterbiOutput`
- `FHSSPulseWordDecoderTest.ReportsLowConfidenceButPreservesValue`
- `FHSSPulseWordDecoderTest.DecodesFromComplexDerivedCpsmDecisionsNotTruthMetadata`

## Implementation Summary

- Added `FHSSPulseWordDecoderNode` as the PR6 CPU-only pulse word conversion helper.
- Converts exactly 32 PR5 CPSM symbol decisions into one `uint32_t`.
- Uses the required bit mapping:
  - `+1 -> 0`
  - `-1 -> 1`
- Uses MSB-first assembly:
  - `value = (value << 1) | bit`
- Preserves `FHSSPulseCandidate` timing/frequency metadata in the decoded output.
- Propagates PR5 confidence and best Viterbi path metric.
- Adds diagnosable decode statuses:
  - `Ok`
  - `InvalidSymbolCount`
  - `InvalidSymbolDecision`
  - `LowConfidence`
  - `InvalidPathMetric`
- Keeps low-confidence output diagnosable while preserving the decoded value for diagnostics.

## Verification Run By Implementer

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSPulseWordDecoderTest.*'`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*'`
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`

All commands passed.

## Scope Notes

- No byte, nibble, or symbol-fragment message model was introduced.
- No truth metadata is used for word decisions.
- No preamble detector, message assembler, graph runtime lane, channelizer, Metal/GPU path, Doppler/noise behavior, or production RF claim was added.
- CMake test wiring is via the existing `libgraph/test/CMakeLists.txt` `CONFIGURE_DEPENDS` unit-test glob; the build reran CMake and included the new test file.
