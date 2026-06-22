# DSP FHSS Decoder PR1 Implementer Report

## PR

PR1 from `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`: FHSS Protocol Types, Frequency Map, And Fixture Schema.

## Files Changed

- `libdsp/include/dsp/fhss/FHSSProtocol.hpp`
  - Added PR1 FHSS protocol constants for the deterministic fixture.
  - Added protocol/config models for frequency entries, timing, truth pulses, detected pulses, pulse candidates, decoded pulses, messages, and decode configuration.
  - Added validation helpers using `std::expected<..., FHSSValidationError>`.
  - Added RF metadata frequency derivation separate from IQ offset frequency.
  - Added timing derivation/validation for `500 Msps`, `5 Mbps`, `100 samples/symbol`, `3200` pulse samples, `3300` gap samples, and `6500` samples/period.
  - Added validation for the 64-entry RF metadata table starting at 1 GHz with 8 MHz spacing.
  - Added selectable-frequency validation for indices `[1, 62]`, with indices `0` and `63` reserved.
  - Added active-set, preamble, payload, message-length, IQ offset/Nyquist guard, and preamble word-consistency validation.
  - Added deterministic payload RNG configuration surface.
- `libgraph/test/unit/test_fhss_protocol.cpp`
  - Added focused PR1 protocol validation tests.

## Files Deleted

- None.

## Tests Added

- `FHSSProtocolTest.TimingModelDerivesSelectedFixtureCounts`
- `FHSSProtocolTest.TimingValidationRejectsNonSelectedSampleRate`
- `FHSSProtocolTest.FrequencyMapDerivesSixtyFourRfMetadataEntries`
- `FHSSProtocolTest.FrequencyConfigRejectsStaleTableShape`
- `FHSSProtocolTest.FrequencyIndexValidationRejectsOutsideTableAndReservedEdges`
- `FHSSProtocolTest.ActiveSetMustContainFourDistinctSelectableFrequencies`
- `FHSSProtocolTest.PreambleRequiresSixteenEntriesInsideActiveSet`
- `FHSSProtocolTest.IdenticalPreambleFrequenciesRequireIdenticalWords`
- `FHSSProtocolTest.PayloadFrequenciesMustComeFromActiveSet`
- `FHSSProtocolTest.MessageLengthIncludesPreambleAndPayload`
- `FHSSProtocolTest.IqOffsetsAreSeparateFromRfMetadataAndGuardedByNyquist`
- `FHSSProtocolTest.DecodeConfigValidatesCompletePr1FixtureSchema`

## Tests Removed

- None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSProtocolTest.*'
```

Result: passed, 12 tests.

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure
```

Result: passed.

## Scope Guardrails

- No signal generator was added.
- No detector, decoder, preamble detector, message assembler, or graph runtime lane was added.
- No plugin runtime, Metal/GPU, real RF capture, channelizer, Doppler/noise behavior, or message assembly behavior was added.
- Absolute RF frequencies remain protocol metadata; fixture IQ offset validation is modeled separately.

## Remaining Follow-Up Work

- PR2 can add the deterministic FHSS CPSM synthetic IQ generator using these protocol contracts.
- PR3 can add detected-pulse metadata merge/association behavior using the PR1 metadata types.
