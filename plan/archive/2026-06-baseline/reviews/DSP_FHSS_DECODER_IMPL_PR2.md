# DSP FHSS Decoder PR2 Implementer Report

## PR

PR2 from `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`: Deterministic FHSS CPSM Synthetic IQ Generator.

## Files Changed

- `libdsp/include/dsp/fhss/FHSSSyntheticIqGenerator.hpp`
  - Added a CPU-only deterministic FHSS CPSM fixture generator.
  - Added `FHSSSyntheticIqGeneratorConfig` and `FHSSSyntheticIqFixture`.
  - Generates complex IQ samples and exact truth metadata.
  - Encodes each `uint32_t` as one 32-symbol binary CPSM pulse.
  - Uses `h = 1/2`, rectangular full-response phase pulse, initial phase `0`, constant envelope, and continuity inside each pulse.
  - Uses `iq_offset_frequency_hz` in the complex exponential and preserves `rf_frequency_hz` as metadata.
  - Generates payload/body frequency indices with deterministic `std::mt19937_64` seeded from PR1 config.
  - Rejects noise, Doppler, multipath, and overlap flags for PR2.
- `libgraph/test/unit/test_fhss_synthetic_iq_generator.cpp`
  - Added focused generator tests.

## Files Deleted

- None.

## Tests Added

- `FHSSSyntheticIqGeneratorTest.EmitsExpectedSampleCountAndTruthMetadata`
- `FHSSSyntheticIqGeneratorTest.PayloadFrequencySelectionIsDeterministic`
- `FHSSSyntheticIqGeneratorTest.RejectsReservedPreambleFrequencies`
- `FHSSSyntheticIqGeneratorTest.RejectsPreambleWordMismatch`
- `FHSSSyntheticIqGeneratorTest.RejectsUnsupportedImpairmentsAndOverlap`
- `FHSSSyntheticIqGeneratorTest.NoiseFreePulseHasConstantEnvelopeAndZeroGap`
- `FHSSSyntheticIqGeneratorTest.UsesIqOffsetNotRfFrequencyInComplexExponential`
- `FHSSSyntheticIqGeneratorTest.RectangularFullResponsePhaseIsContinuous`
- `FHSSSyntheticIqGeneratorTest.RejectsMessageLongerThanTwoHundredFiftySixPulses`

## Tests Removed

- None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSSyntheticIqGeneratorTest.*'
```

Result: passed, 9 tests.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*'
```

Result: passed, 21 tests.

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure
```

Result: passed.

## Implementation Notes

- The generated sample buffer includes `N_period = 6500` samples per pulse slot: `3200` pulse samples followed by `3300` zero-valued gap samples.
- Truth pulse `global_start_sample` values are exact slot starts: `pulse_index * 6500`.
- Payload/body frequencies are generated inside PR2 from the four active preamble frequencies rather than supplied as truth metadata.
- The rectangular full-response phase pulse uses a linear `q(t)` ramp from `0` toward `1/2` across each symbol and accumulates completed symbol phase continuously inside a pulse.

## Scope Guardrails

- No detector was added.
- No decoder, preamble detector, message assembler, or graph runtime integration was added.
- No plugin runtime, channelizer, Metal/GPU, real RF capture, Doppler/noise behavior, multipath behavior, overlap behavior, or production RF claim was added.

## Remaining Follow-Up Work

- PR3 can add detected-pulse metadata and merge/association behavior.
- PR4 can add the correlator-bank detector that consumes generated IQ fixtures.
- PR5 must use the same rectangular full-response CPSM assumptions when implementing branch metrics and Viterbi/MLSE.
