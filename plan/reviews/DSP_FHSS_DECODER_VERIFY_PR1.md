# DSP FHSS Decoder PR1 Verifier Report

## PR

PR1 from `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`: FHSS Protocol Types, Frequency Map, And Fixture Schema.

## Verdict

Pass.

## Findings

No blocking or non-blocking PR1 issues found.

## Required Checks

- FHSS protocol/type/config model exists and compiles independently: pass.
  - Implemented in `libdsp/include/dsp/fhss/FHSSProtocol.hpp`.
  - Compiled through `test_libgraph_unit`.
- Frequency table derives exactly 64 RF metadata entries from 1 GHz at 8 MHz spacing: pass.
  - Covered by `FHSSProtocolTest.FrequencyMapDerivesSixtyFourRfMetadataEntries`.
- Validation rejects frequency indices outside `[0, 63]`: pass.
  - Covered by `FHSSProtocolTest.FrequencyIndexValidationRejectsOutsideTableAndReservedEdges`.
- Validation rejects reserved edge indices `0` and `63` for active preamble and payload/body selection: pass.
  - Covered by active-set, selectable-index, and payload-frequency tests.
- Active preamble validation requires exactly four distinct selectable indices: pass.
  - Covered by `FHSSProtocolTest.ActiveSetMustContainFourDistinctSelectableFrequencies`.
- Preamble validation requires exactly 16 pulses: pass.
  - Covered by `FHSSProtocolTest.PreambleRequiresSixteenEntriesInsideActiveSet`.
- Message length validation rejects more than 256 pulses including preamble: pass.
  - Covered by `FHSSProtocolTest.MessageLengthIncludesPreambleAndPayload`.
- Timing validation proves `500 Msps / 5 Mbps = 100`, `N_pulse = 3200`, `N_gap = 3300`, and `N_period = 6500`: pass.
  - Covered by `FHSSProtocolTest.TimingModelDerivesSelectedFixtureCounts`.
- RF metadata frequency and IQ offset frequency are distinct and validated: pass.
  - Covered by `FHSSProtocolTest.IqOffsetsAreSeparateFromRfMetadataAndGuardedByNyquist`.
- Full-table validation documents that all 64 RF centers cannot be represented alias-free in one `500 Msps` complex-baseband span while preserving 8 MHz spacing: pass.
  - The implementation keeps RF frequency metadata separate from IQ offsets and rejects RF-as-IQ active offsets through Nyquist guard validation.
- No generator, detector, decoder, graph runtime, Metal/GPU, real RF capture, channelizer, or message assembly was added: pass.
  - Scope scan found only `libdsp/include/dsp/fhss/FHSSProtocol.hpp` and `libgraph/test/unit/test_fhss_protocol.cpp` under code paths for FHSS.

## Tests Run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: passed, no work to do.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSProtocolTest.*'
```

Result: passed, 12 tests.

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure
```

Result: passed.

## Residual Risk

- PR1 intentionally provides protocol contracts and validation only. Later PRs must still prove that generator, detector, decoder, and graph runtime code consume these contracts without bypassing validation.
- The deterministic RNG surface is configuration-only in PR1, as expected; actual deterministic payload/body frequency selection belongs to PR2.
