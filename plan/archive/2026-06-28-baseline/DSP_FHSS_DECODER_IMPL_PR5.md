# DSP FHSS Decoder PR5 Implementer Report

Role: IMPLEMENTER

PR: PR5 - CPSM Branch Metric And Viterbi/MLSE Decoder

Verdict: Implemented

## Files Changed

- `libdsp/include/dsp/fhss/FHSSCpsmDecoder.hpp`
- `libgraph/test/unit/test_fhss_cpsm_decoder.cpp`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR5.md`

## Files Deleted

- None.

## Tests Added

- `FHSSCpsmDecoderTest.RectangularFullResponseThetaIsContinuous`
- `FHSSCpsmDecoderTest.TrellisTransitionsAreAccumulatedPhaseModuloTwoPi`
- `FHSSCpsmDecoderTest.BranchMetricFavorsMatchingSymbolAndState`
- `FHSSCpsmDecoderTest.KnownGeneratedPulseDecodesToSymbols`
- `FHSSCpsmDecoderTest.ViterbiMatchesReducedBruteForceOracle`
- `FHSSCpsmDecoderTest.TerminalPhasePolicyCanBeCheckedOrUnconstrained`
- `FHSSCpsmDecoderTest.MagnitudeOnlyInputIsImpossibleByDecoderType`
- `FHSSCpsmDecoderTest.InvalidEvidenceLengthIsRejected`

## Implementation Summary

- Added `CPSMBranchMetricNode` and `CPSMViterbiDecoderNode` as CPU-only, one-pulse fixture helpers.
- Pinned the PR5 CPSM estimator to the existing PR2 generator fixture:
  - `h = 1/2`
  - binary symbols `a[k] in {-1,+1}`
  - initial phase state `0`
  - rectangular full-response phase pulse using the PR2 normalization
  - four accumulated phase states modulo `2*pi`
  - continuity inside each pulse only
  - terminal phase unconstrained by default, with optional checked terminal state
- Defined branch metrics over complex dehopped evidence:
  - normalize each non-zero complex sample to unit magnitude
  - compare against the predicted CPM unit phasor for the candidate branch
  - use mean coherent correlation and cost `1 - correlation`
- Implemented Viterbi/MLSE over `O(symbol_count * 4 * 2)` transitions.
- Kept brute force limited to a reduced-length test oracle only.
- Kept decoder output at symbol decisions and path metrics only; no `uint32_t` pulse-word mapping was added.

## Verification Run By Implementer

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSCpsmDecoderTest.*'`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*'`
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`

All commands passed.

## Scope Notes

- No `2^32` pulse-word enumeration path was added.
- No truth metadata is used for decoder decisions.
- No pulse-word decoder, preamble detector, message assembler, graph runtime lane, channelizer, Metal/GPU path, Doppler/noise behavior, or production RF claim was added.
- CMake test wiring is via the existing `libgraph/test/CMakeLists.txt` `CONFIGURE_DEPENDS` unit-test glob; the build reran CMake and included the new test file.
